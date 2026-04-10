#include "engine_api.h"

#if defined(ENGINE_API_USE_KRKR2_RUNTIME)

#include <algorithm>
#include <cstdarg>
#include <chrono>
#include <cmath>
#include <cstring>
#include <deque>
#include <new>
#include <string>
#include <unordered_set>
#include <mutex>
#include <thread>
#include <memory>
#include <vector>

#include <csignal>
#include <cstdlib>
#if defined(__ANDROID__)
#include <android/log.h>
#include <android/native_window.h>
// Defined in krkr2_android.cpp (C++ linkage)
ANativeWindow* krkr_GetNativeWindow();
void krkr_GetSurfaceDimensions(uint32_t*, uint32_t*);
#endif
#if !defined(__ANDROID__)
#include <execinfo.h>
#endif

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/sink.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>

#include "environ/Application.h"
#include "environ/Platform.h"
#include "environ/EngineBootstrap.h"
#include "environ/EngineLoop.h"
#include "environ/MainScene.h"
#include "base/StorageIntf.h"
#include "base/SysInitIntf.h"
#include "base/impl/SysInitImpl.h"
#include "visual/GraphicsLoaderIntf.h"
#include "visual/ogl/ogl_common.h"
#include "visual/ogl/krkr_egl_context.h"
#include "visual/ogl/angle_backend.h"
#include "visual/impl/WindowImpl.h"
#include "visual/RenderManager.h"
#include "psbfile/PSBMedia.h"
#include "engine_options.h"
#include "PluginCallTracer.hpp"

// Mock bypass toggle (defined in tjsVariant.cpp, namespace TJS)
namespace TJS { void TVPSetMockEnabled(bool enabled); }

// Console log file toggle (default ON — existing behavior)
static bool g_ConsoleLogFileEnabled = true;

bool TVPIsConsoleLogFileEnabled() { return g_ConsoleLogFileEnabled; }

int TVPDrawSceneOnce(int interval);

extern "C" void TVPRegisterKrkrGLESPluginAnchor();
extern "C" void TVPRegisterKrkrLive2DPluginAnchor();

struct engine_handle_s {
  std::recursive_mutex mutex;
  std::string last_error;
  int state = 0;
  std::thread::id owner_thread;
  bool runtime_owner = false;
  uint64_t tick_count = 0;

  // Frame state — readback buffer and tracking
  struct FrameState {
    uint32_t surface_width = 1280;
    uint32_t surface_height = 720;
    uint64_t serial = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t stride_bytes = 0;
    std::vector<uint8_t> rgba;
    bool ready = false;
    bool rendered_this_tick = false;
  } frame;

  // Frame rate limiting (0 = unlimited / follow vsync)
  struct FpsLimitState {
    uint32_t limit = 0;
    uint64_t interval_us = 0;
    std::chrono::steady_clock::time_point last_render_time{};
    bool initialized = false;
  } fps;

  // Input event queue
  struct InputState {
    std::deque<engine_input_event_t> pending_events;
    std::unordered_set<intptr_t> active_pointer_ids;
    bool native_mouse_callbacks_disabled = false;
  } input;

  // Render target state
  struct RenderTargetState {
    krkr::AngleBackend angle_backend = krkr::AngleBackend::OpenGLES;
    bool iosurface_attached = false;
    bool native_window_attached = false;
  } render;

  struct StartupState {
    std::mutex mutex;
    uint32_t state = ENGINE_STARTUP_STATE_IDLE;
    std::deque<std::string> logs;
    std::thread worker;
    bool worker_running = false;
  } startup;

  struct MemoryOptionState {
    int psb_cache_mb = 0;
    int psb_cache_entries = 0;
  } memory_options;
};

namespace {

#if defined(__ANDROID__)
void AndroidInfoLog(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  __android_log_vprint(ANDROID_LOG_INFO, "krkr2", fmt, args);
  va_end(args);
}
#endif

enum class EngineState {
  kCreated = 0,
  kOpened,
  kPaused,
  kDestroyed,
};

inline int ToStateValue(EngineState state) {
  return static_cast<int>(state);
}

std::recursive_mutex g_registry_mutex;
std::unordered_set<engine_handle_t> g_live_handles;
thread_local std::string g_thread_error;
engine_handle_t g_runtime_owner = nullptr;
bool g_runtime_active = false;
bool g_runtime_started_once = false;
bool g_engine_bootstrapped = false;
bool g_runtime_startup_active = false;
engine_handle_t g_runtime_startup_owner = nullptr;
std::once_flag g_loggers_init_once;
std::shared_ptr<spdlog::sinks::sink> g_startup_log_sink;
constexpr size_t kMaxStartupLogs = 4000;

void PushRuntimeSpdlogToStartupQueue(const spdlog::details::log_msg& msg);

class StartupLogSink final : public spdlog::sinks::sink {
 public:
  void log(const spdlog::details::log_msg& msg) override {
    PushRuntimeSpdlogToStartupQueue(msg);
  }

  void flush() override {}
  void set_pattern(const std::string&) override {}
  void set_formatter(std::unique_ptr<spdlog::formatter>) override {}
};

std::shared_ptr<spdlog::logger> EnsureNamedLogger(const char* name) {
  if (auto logger = spdlog::get(name); logger != nullptr) {
    return logger;
  }
  return spdlog::stdout_color_mt(name);
}

void CrashSignalHandler(int sig) {
  spdlog::critical("FATAL SIGNAL {} received!", sig);

  // Print a mini backtrace (not available on Android)
#if !defined(__ANDROID__)
  void* frames[32];
  int count = backtrace(frames, 32);
  char** symbols = backtrace_symbols(frames, count);
  if (symbols) {
    for (int i = 0; i < count; ++i) {
      spdlog::critical("  [{}] {}", i, symbols[i]);
    }
    free(symbols);
  }
#endif

  spdlog::default_logger()->flush();
  // Re-raise so the OS generates a proper crash report
  signal(sig, SIG_DFL);
  raise(sig);
}

void InstallCrashSignalHandlers() {
  signal(SIGSEGV, CrashSignalHandler);
  signal(SIGABRT, CrashSignalHandler);
  signal(SIGBUS,  CrashSignalHandler);
  signal(SIGFPE,  CrashSignalHandler);
}

void EnsureInternalPluginAnchorsLinked() {
  TVPRegisterKrkrGLESPluginAnchor();
  TVPRegisterKrkrLive2DPluginAnchor();
}

void EnsureRuntimeLoggersInitialized() {
  std::call_once(g_loggers_init_once, []() {
    spdlog::set_level(spdlog::level::debug);
    // Flush every log message so crash logs are never lost
    spdlog::flush_on(spdlog::level::debug);
    auto core_logger = EnsureNamedLogger("core");
    auto tjs2_logger = EnsureNamedLogger("tjs2");
    auto plugin_logger = EnsureNamedLogger("plugin");
    g_startup_log_sink = std::make_shared<StartupLogSink>();
    auto attach_sink = [](const std::shared_ptr<spdlog::logger>& logger) {
      if (logger == nullptr || g_startup_log_sink == nullptr) {
        return;
      }
      auto& sinks = logger->sinks();
      const auto already_attached = std::any_of(
          sinks.begin(), sinks.end(),
          [](const std::shared_ptr<spdlog::sinks::sink>& sink) {
            return sink.get() == g_startup_log_sink.get();
          });
      if (!already_attached) {
        sinks.push_back(g_startup_log_sink);
      }
    };
    attach_sink(core_logger);
    attach_sink(tjs2_logger);
    attach_sink(plugin_logger);
    if (core_logger != nullptr) {
      spdlog::set_default_logger(core_logger);
    }
    InstallCrashSignalHandlers();
  });
}

void SetThreadError(const char* message) {
  g_thread_error = (message != nullptr) ? message : "";
}

engine_result_t SetThreadErrorAndReturn(engine_result_t result,
                                        const char* message) {
  SetThreadError(message);
  return result;
}

bool IsHandleLiveLocked(engine_handle_t handle) {
  return g_live_handles.find(handle) != g_live_handles.end();
}

engine_result_t ValidateHandleLocked(engine_handle_t handle,
                                     engine_handle_s** out_impl) {
  if (handle == nullptr) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "engine handle is null");
  }
  if (!IsHandleLiveLocked(handle)) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "engine handle is invalid or already destroyed");
  }
  *out_impl = reinterpret_cast<engine_handle_s*>(handle);
  return ENGINE_RESULT_OK;
}

void SetHandleErrorLocked(engine_handle_s* impl, const char* message) {
  impl->last_error = (message != nullptr) ? message : "";
}

engine_result_t SetHandleErrorAndReturnLocked(engine_handle_s* impl,
                                              engine_result_t result,
                                              const char* message) {
  SetHandleErrorLocked(impl, message);
  return result;
}

engine_result_t ValidateHandleThreadLocked(engine_handle_s* impl) {
  if (impl->owner_thread != std::this_thread::get_id()) {
    return SetHandleErrorAndReturnLocked(
        impl,
        ENGINE_RESULT_INVALID_STATE,
        "engine handle must be used on the thread where engine_create was called");
  }
  return ENGINE_RESULT_OK;
}

void ClearHandleErrorLocked(engine_handle_s* impl) {
  impl->last_error.clear();
}

void PushStartupLog(engine_handle_s* impl, const std::string& message) {
  std::lock_guard<std::mutex> guard(impl->startup.mutex);
  impl->startup.logs.push_back(message);
  while (impl->startup.logs.size() > kMaxStartupLogs) {
    impl->startup.logs.pop_front();
  }
}

void PushRuntimeSpdlogToStartupQueue(const spdlog::details::log_msg& msg) {
  engine_handle_s* target = nullptr;
  {
    std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
    if (!g_runtime_startup_active || g_runtime_startup_owner == nullptr) {
      return;
    }
    if (!IsHandleLiveLocked(g_runtime_startup_owner)) {
      return;
    }
    target = reinterpret_cast<engine_handle_s*>(g_runtime_startup_owner);
  }
  if (target == nullptr) {
    return;
  }

  const auto level_sv = spdlog::level::to_string_view(msg.level);
  const std::string level(level_sv.data(), level_sv.size());
  std::string logger(msg.logger_name.data(), msg.logger_name.size());
  if (logger.empty()) {
    logger = "core";
  }
  const std::string payload(msg.payload.data(), msg.payload.size());

  std::string line;
  line.reserve(logger.size() + level.size() + payload.size() + 8);
  line.append("[");
  line.append(logger);
  line.append("] [");
  line.append(level);
  line.append("] ");
  line.append(payload);
  PushStartupLog(target, line);
}

void SetStartupState(engine_handle_s* impl, uint32_t state) {
  std::lock_guard<std::mutex> guard(impl->startup.mutex);
  impl->startup.state = state;
}

uint32_t GetStartupState(engine_handle_s* impl) {
  std::lock_guard<std::mutex> guard(impl->startup.mutex);
  return impl->startup.state;
}

void ResetStartupState(engine_handle_s* impl) {
  std::lock_guard<std::mutex> guard(impl->startup.mutex);
  impl->startup.state = ENGINE_STARTUP_STATE_IDLE;
  impl->startup.logs.clear();
}

void MarkStartupWorkerRunning(engine_handle_s* impl, bool running) {
  std::lock_guard<std::mutex> guard(impl->startup.mutex);
  impl->startup.worker_running = running;
}

std::thread DetachStartupWorker(engine_handle_s* impl) {
  std::lock_guard<std::mutex> guard(impl->startup.mutex);
  if (!impl->startup.worker.joinable()) {
    return std::thread();
  }
  return std::move(impl->startup.worker);
}

bool EnsureEngineRuntimeInitialized(uint32_t width, uint32_t height,
                                    krkr::AngleBackend backend = krkr::AngleBackend::OpenGLES) {
  if (g_engine_bootstrapped) {
    return true;
  }
  if (!TVPEngineBootstrap::Initialize(width, height, backend)) {
    return false;
  }
  g_engine_bootstrapped = true;
  return true;
}

struct FrameReadbackLayout {
  int32_t read_x = 0;
  int32_t read_y = 0;
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t stride_bytes = 0;
};

FrameReadbackLayout GetFrameReadbackLayoutLocked(engine_handle_s* impl) {
  FrameReadbackLayout layout;
  layout.width = impl->frame.surface_width;
  layout.height = impl->frame.surface_height;

  GLint viewport[4] = {0, 0, 0, 0};
  glGetIntegerv(GL_VIEWPORT, viewport);
  if (glGetError() == GL_NO_ERROR && viewport[2] > 0 && viewport[3] > 0) {
    layout.read_x = viewport[0];
    layout.read_y = viewport[1];
    layout.width = static_cast<uint32_t>(viewport[2]);
    layout.height = static_cast<uint32_t>(viewport[3]);
  } else {
    // Fallback: use the EGL surface dimensions
    auto& egl = krkr::GetEngineEGLContext();
    if (egl.IsValid()) {
      const uint32_t egl_w = egl.GetWidth();
      const uint32_t egl_h = egl.GetHeight();
      if (egl_w > 0 && egl_h > 0) {
        layout.width = egl_w;
        layout.height = egl_h;
      }
    }
  }

  if (layout.width == 0) {
    layout.width = 1;
  }
  if (layout.height == 0) {
    layout.height = 1;
  }
  layout.stride_bytes = layout.width * 4u;
  return layout;
}

bool ReadCurrentFrameRgba(const FrameReadbackLayout& layout, void* out_pixels) {
  if (layout.width == 0 || layout.height == 0 || out_pixels == nullptr) {
    return false;
  }

  glFinish();
  glPixelStorei(GL_PACK_ALIGNMENT, 4);
  glReadPixels(static_cast<GLint>(layout.read_x),
               static_cast<GLint>(layout.read_y),
               static_cast<GLsizei>(layout.width),
               static_cast<GLsizei>(layout.height), GL_RGBA,
               GL_UNSIGNED_BYTE, out_pixels);
  const GLenum read_pixels_error = glGetError();

  if (read_pixels_error != GL_NO_ERROR) {
    return false;
  }

  auto* bytes = static_cast<uint8_t*>(out_pixels);
  const size_t row_bytes = static_cast<size_t>(layout.stride_bytes);
  std::vector<uint8_t> row_buffer(row_bytes);
  const uint32_t half_rows = layout.height / 2u;
  for (uint32_t y = 0; y < half_rows; ++y) {
    uint8_t* row_top = bytes + static_cast<size_t>(y) * row_bytes;
    uint8_t* row_bottom =
        bytes + static_cast<size_t>(layout.height - 1u - y) * row_bytes;
    std::memcpy(row_buffer.data(), row_top, row_bytes);
    std::memcpy(row_top, row_bottom, row_bytes);
    std::memcpy(row_bottom, row_buffer.data(), row_bytes);
  }

  return true;
}

bool IsFinitePointerValue(double value) {
  return std::isfinite(value);
}

engine_result_t DispatchInputEventNow(engine_handle_s* impl,
                                      const engine_input_event_t& event,
                                      const char** out_error_message) {
  auto* loop = EngineLoop::GetInstance();
  if (loop == nullptr) {
    if (out_error_message != nullptr) {
      *out_error_message = "engine loop is unavailable";
    }
    return ENGINE_RESULT_INVALID_STATE;
  }

  // Convert engine_input_event_t → EngineInputEvent (bridge → core)
  EngineInputEvent core_event;
  core_event.type = event.type;
  core_event.x = event.x;
  core_event.y = event.y;
  core_event.delta_x = event.delta_x;
  core_event.delta_y = event.delta_y;
  core_event.pointer_id = event.pointer_id;
  core_event.button = event.button;
  core_event.key_code = event.key_code;
  core_event.modifiers = event.modifiers;
  core_event.unicode_codepoint = event.unicode_codepoint;

  if (!loop->HandleInputEvent(core_event)) {
    if (out_error_message != nullptr) {
      *out_error_message = "input event dispatch failed (no active window?)";
    }
    return ENGINE_RESULT_INVALID_STATE;
  }

  if (out_error_message != nullptr) {
    *out_error_message = nullptr;
  }
  return ENGINE_RESULT_OK;
}

engine_result_t OpenGameCore(engine_handle_t handle,
                             engine_handle_s* impl,
                             const char* game_root_path_utf8) {
  if (game_root_path_utf8 == nullptr || game_root_path_utf8[0] == '\0') {
    return ENGINE_RESULT_INVALID_ARGUMENT;
  }

  if (!EnsureEngineRuntimeInitialized(impl->frame.surface_width,
                                      impl->frame.surface_height,
                                      impl->render.angle_backend)) {
    std::lock_guard<std::recursive_mutex> guard(impl->mutex);
    SetHandleErrorLocked(impl, "failed to initialize engine runtime for host mode");
    return ENGINE_RESULT_INTERNAL_ERROR;
  }

  EnsureRuntimeLoggersInitialized();
  EnsureInternalPluginAnchorsLinked();
  // Cache options set via engine_set_option() are already stored in
  // TVPEarlySetOptions and will be merged during TVPSystemInit().
  // Do NOT call TVPGetCommandLine() here — it triggers
  // TVPInitProgramArgumentsAndDataPath() which caches TVPGetAppPath()
  // as empty (TVPProjectDir is not set yet), corrupting path resolution.

  std::string normalized_game_root_path(game_root_path_utf8);
  // Only append trailing slash for directory paths.  Archive files
  // (.xp3, .zip, etc.) must keep their original extension so the
  // storage system can recognise them as archives.
  auto ends_with = [](const std::string &s, const std::string &suffix) {
    return s.size() >= suffix.size() &&
           s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
  };
  const bool looks_like_archive =
      ends_with(normalized_game_root_path, ".xp3") ||
      ends_with(normalized_game_root_path, ".XP3") ||
      ends_with(normalized_game_root_path, ".zip") ||
      ends_with(normalized_game_root_path, ".ZIP") ||
      ends_with(normalized_game_root_path, ".7z")  ||
      ends_with(normalized_game_root_path, ".tar");
  if (!looks_like_archive &&
      !normalized_game_root_path.empty() &&
      normalized_game_root_path.back() != '/' &&
      normalized_game_root_path.back() != '\\') {
    normalized_game_root_path.push_back('/');
  }

  spdlog::info(
      "engine_open_game: runtime initialized, starting application with path: {} (normalized: {})",
      game_root_path_utf8, normalized_game_root_path);
#if defined(__ANDROID__)
  AndroidInfoLog("engine_open_game: input='%s' normalized='%s'",
                 game_root_path_utf8, normalized_game_root_path.c_str());
  try {
    std::string log_file_path = normalized_game_root_path;
    if (!log_file_path.empty() && log_file_path.back() != '/') {
      log_file_path += "/";
    }
    log_file_path += "krkr2.log";
    auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(log_file_path, true);
    auto attach_sink = [&](const char* name) {
      if (auto logger = spdlog::get(name)) {
        logger->sinks().push_back(file_sink);
      }
    };
    attach_sink("core");
    attach_sink("tjs2");
    attach_sink("plugin");
    spdlog::info("engine_open_game: Android file logger successfully attached to {}", log_file_path);
  } catch (const std::exception& e) {
    AndroidInfoLog("engine_open_game: Failed to create Android log file: %s", e.what());
  }
#endif
  spdlog::default_logger()->flush();

  // Only initialize plugin trace logger when tracing is enabled.
  // The toggle is set via engine_set_option before engine_open_game.
  if (PluginCallTracer::Instance().IsEnabled()) {
    std::string trace_path = normalized_game_root_path;
    if (!trace_path.empty() && trace_path.back() != '/') trace_path += "/";
    trace_path += "plugin_trace.log";
    PluginCallTracer::Instance().InitLogger(trace_path);
  }

  try {
    spdlog::debug("engine_open_game: calling Application->StartApplication...");
#if defined(__ANDROID__)
    AndroidInfoLog("engine_open_game: calling StartApplication('%s')",
                   normalized_game_root_path.c_str());
#endif
    spdlog::default_logger()->flush();
    Application->StartApplication(ttstr(normalized_game_root_path.c_str()));
    spdlog::info("engine_open_game: StartApplication returned successfully");
#if defined(__ANDROID__)
    AndroidInfoLog("engine_open_game: StartApplication returned successfully");
#endif
  } catch (const std::exception& e) {
    spdlog::error("engine_open_game: StartApplication threw std::exception: {}",
                  e.what());
    std::lock_guard<std::recursive_mutex> guard(impl->mutex);
    SetHandleErrorLocked(impl, "StartApplication threw an exception");
    return ENGINE_RESULT_INTERNAL_ERROR;
  } catch (...) {
    spdlog::error("engine_open_game: StartApplication threw unknown exception");
    std::lock_guard<std::recursive_mutex> guard(impl->mutex);
    SetHandleErrorLocked(impl, "StartApplication threw an exception");
    return ENGINE_RESULT_INTERNAL_ERROR;
  }

  if (TVPTerminated) {
    std::lock_guard<std::recursive_mutex> guard(impl->mutex);
    SetHandleErrorLocked(impl, "runtime requested termination during startup");
    return ENGINE_RESULT_INVALID_STATE;
  }

  EngineLoop::CreateInstance();
  if (auto* loop = EngineLoop::GetInstance(); loop != nullptr) {
    loop->Start();
  }

  if (auto* scene = TVPMainScene::GetInstance(); scene != nullptr) {
    scene->scheduleUpdate();
  }

  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  if (!IsHandleLiveLocked(handle)) {
    return ENGINE_RESULT_INVALID_STATE;
  }

  std::lock_guard<std::recursive_mutex> guard(impl->mutex);
  if (impl->state == ToStateValue(EngineState::kDestroyed)) {
    return ENGINE_RESULT_INVALID_STATE;
  }

  if (g_runtime_startup_active && g_runtime_startup_owner == handle) {
    g_runtime_startup_active = false;
    g_runtime_startup_owner = nullptr;
  }
  g_runtime_active = true;
  g_runtime_owner = handle;
  g_runtime_started_once = true;

  impl->runtime_owner = true;
  impl->input.native_mouse_callbacks_disabled = true;
  impl->frame.width = 0;
  impl->frame.height = 0;
  impl->frame.stride_bytes = 0;
  impl->frame.rgba.clear();
  impl->frame.ready = false;
  impl->input.active_pointer_ids.clear();
  impl->input.pending_events.clear();
  impl->state = ToStateValue(EngineState::kOpened);
  ClearHandleErrorLocked(impl);
  return ENGINE_RESULT_OK;
}

void RunOpenGameAsync(engine_handle_t handle,
                      engine_handle_s* impl,
                      std::string game_root_path_utf8) {
  PushStartupLog(impl, "engine_open_game_async: worker started");
  TVPTerminated = false;
  TVPTerminateCode = 0;
  TVPSystemUninitCalled = false;
  TVPTerminateOnWindowClose = false;
  TVPTerminateOnNoWindowStartup = false;
  TVPHostSuppressProcessExit = true;

  const engine_result_t open_result =
      OpenGameCore(handle, impl, game_root_path_utf8.c_str());

  // Startup runs on a worker thread; release current GL context here so the
  // owner thread can safely make it current before ticking/rendering.
  auto& egl = krkr::GetEngineEGLContext();
  if (egl.IsValid()) {
    egl.ReleaseCurrent();
  }

  if (open_result == ENGINE_RESULT_OK) {
    PushStartupLog(impl, "engine_open_game => OK");
    SetStartupState(impl, ENGINE_STARTUP_STATE_SUCCEEDED);
  } else {
    std::string error_text;
    {
      std::lock_guard<std::recursive_mutex> guard(impl->mutex);
      error_text = impl->last_error;
    }
    if (error_text.empty()) {
      error_text = "unknown startup error";
    }
    PushStartupLog(impl, "ERROR: " + error_text);
    SetStartupState(impl, ENGINE_STARTUP_STATE_FAILED);
    std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
    if (g_runtime_startup_active && g_runtime_startup_owner == handle) {
      g_runtime_startup_active = false;
      g_runtime_startup_owner = nullptr;
    }
  }
  MarkStartupWorkerRunning(impl, false);
}

}  // namespace

extern std::string TVPEngineApi_GetGlobalException();

extern "C" {

engine_result_t engine_get_runtime_api_version(uint32_t* out_api_version) {
  if (out_api_version == nullptr) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "out_api_version is null");
  }
  *out_api_version = ENGINE_API_VERSION;
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_create(const engine_create_desc_t* desc,
                              engine_handle_t* out_handle) {
  if (desc == nullptr || out_handle == nullptr) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "engine_create requires non-null desc and out_handle");
  }

  if (desc->struct_size < sizeof(engine_create_desc_t)) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "engine_create_desc_t.struct_size is too small");
  }

  const uint32_t expected_major = (ENGINE_API_VERSION >> 24u) & 0xFFu;
  const uint32_t caller_major = (desc->api_version >> 24u) & 0xFFu;
  if (caller_major != expected_major) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_NOT_SUPPORTED,
                                   "unsupported engine API major version");
  }

  EnsureRuntimeLoggersInitialized();
  EnsureInternalPluginAnchorsLinked();
  TVPHostSuppressProcessExit = true;

  auto* impl = new (std::nothrow) engine_handle_s();
  if (impl == nullptr) {
    *out_handle = nullptr;
    return SetThreadErrorAndReturn(ENGINE_RESULT_INTERNAL_ERROR,
                                   "failed to allocate engine handle");
  }

  impl->state = ToStateValue(EngineState::kCreated);
  impl->owner_thread = std::this_thread::get_id();
  impl->runtime_owner = false;

  auto handle = reinterpret_cast<engine_handle_t>(impl);
  {
    std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
    g_live_handles.insert(handle);
  }

  *out_handle = handle;
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_destroy(engine_handle_t handle) {
  if (handle == nullptr) {
    SetThreadError(nullptr);
    return ENGINE_RESULT_OK;
  }

  engine_handle_s* impl = nullptr;
  bool owned_runtime = false;
  std::thread startup_worker;

  {
    std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
    auto result = ValidateHandleLocked(handle, &impl);
    if (result != ENGINE_RESULT_OK) {
      return result;
    }

    std::lock_guard<std::recursive_mutex> guard(impl->mutex);
    result = ValidateHandleThreadLocked(impl);
    if (result != ENGINE_RESULT_OK) {
      return result;
    }

    owned_runtime = (g_runtime_active && g_runtime_owner == handle);
    if (owned_runtime) {
      g_runtime_active = false;
      g_runtime_owner = nullptr;
      impl->runtime_owner = false;
    }
    if (g_runtime_startup_active && g_runtime_startup_owner == handle) {
      g_runtime_startup_active = false;
      g_runtime_startup_owner = nullptr;
    }
    startup_worker = DetachStartupWorker(impl);
    SetStartupState(impl, ENGINE_STARTUP_STATE_IDLE);

    impl->state = ToStateValue(EngineState::kDestroyed);
    ClearHandleErrorLocked(impl);
    g_live_handles.erase(handle);
  }

  if (startup_worker.joinable()) {
    startup_worker.join();
  }

  if (owned_runtime) {
    try {
      Application->OnDeactivate();
    } catch (...) {
    }
    Application->FilterUserMessage(
        [](std::vector<std::tuple<void*, int, tTVPApplication::tMsg>>& queue) {
          queue.clear();
        });

    // Avoid triggering platform exit() path in the host process.
    TVPTerminated = false;
    TVPTerminateCode = 0;
  }

  delete impl;
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_open_game(engine_handle_t handle,
                                 const char* game_root_path_utf8,
                                 const char* startup_script_utf8) {
  (void)startup_script_utf8;

  if (game_root_path_utf8 == nullptr || game_root_path_utf8[0] == '\0') {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "game_root_path_utf8 is null or empty");
  }

  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  engine_handle_s* impl = nullptr;
  auto result = ValidateHandleLocked(handle, &impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  std::lock_guard<std::recursive_mutex> guard(impl->mutex);
  result = ValidateHandleThreadLocked(impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  if (impl->state == ToStateValue(EngineState::kDestroyed)) {
    return SetHandleErrorAndReturnLocked(impl, ENGINE_RESULT_INVALID_STATE,
                                         "engine is already destroyed");
  }
  if (g_runtime_startup_active && g_runtime_startup_owner == handle) {
    return SetHandleErrorAndReturnLocked(
        impl, ENGINE_RESULT_INVALID_STATE, "engine startup is already running");
  }

  if (g_runtime_active) {
    if (g_runtime_owner != handle) {
      return SetHandleErrorAndReturnLocked(
          impl,
          ENGINE_RESULT_INVALID_STATE,
          "runtime is already active on another engine handle");
    }

    impl->state = ToStateValue(EngineState::kOpened);
    ClearHandleErrorLocked(impl);
    SetThreadError(nullptr);
    return ENGINE_RESULT_OK;
  }

  if (g_runtime_started_once) {
    return SetHandleErrorAndReturnLocked(
        impl,
        ENGINE_RESULT_NOT_SUPPORTED,
        "runtime restart is not supported yet; restart process to open another game");
  }
  TVPTerminated = false;
  TVPTerminateCode = 0;
  TVPSystemUninitCalled = false;
  TVPTerminateOnWindowClose = false;
  TVPTerminateOnNoWindowStartup = false;
  TVPHostSuppressProcessExit = true;
  g_runtime_startup_active = true;
  g_runtime_startup_owner = handle;
  ResetStartupState(impl);
  SetStartupState(impl, ENGINE_STARTUP_STATE_RUNNING);
  PushStartupLog(impl, "engine_open_game: starting");
  ClearHandleErrorLocked(impl);

  const engine_result_t open_result =
      OpenGameCore(handle, impl, game_root_path_utf8);
  if (open_result == ENGINE_RESULT_OK) {
    SetStartupState(impl, ENGINE_STARTUP_STATE_SUCCEEDED);
    PushStartupLog(impl, "engine_open_game => OK");
    SetThreadError(nullptr);
    return ENGINE_RESULT_OK;
  }

  if (open_result == ENGINE_RESULT_INTERNAL_ERROR) {
    SetHandleErrorLocked(impl, "StartApplication threw an exception");
  } else if (open_result == ENGINE_RESULT_INVALID_STATE && TVPTerminated) {
    SetHandleErrorLocked(impl, "runtime requested termination during startup");
  } else {
    SetHandleErrorLocked(impl, "engine_open_game failed");
  }
  g_runtime_startup_active = false;
  g_runtime_startup_owner = nullptr;
  SetStartupState(impl, ENGINE_STARTUP_STATE_FAILED);
  PushStartupLog(impl, std::string("ERROR: ") + impl->last_error);
  return open_result;
}

engine_result_t engine_open_game_async(engine_handle_t handle,
                                       const char* game_root_path_utf8,
                                       const char* startup_script_utf8) {
  (void)startup_script_utf8;

  if (game_root_path_utf8 == nullptr || game_root_path_utf8[0] == '\0') {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "game_root_path_utf8 is null or empty");
  }

  std::thread stale_worker;
  std::string game_root_copy(game_root_path_utf8);
  engine_handle_s* impl = nullptr;
  {
    std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
    auto result = ValidateHandleLocked(handle, &impl);
    if (result != ENGINE_RESULT_OK) {
      return result;
    }

    std::lock_guard<std::recursive_mutex> guard(impl->mutex);
    result = ValidateHandleThreadLocked(impl);
    if (result != ENGINE_RESULT_OK) {
      return result;
    }

    if (impl->state == ToStateValue(EngineState::kDestroyed)) {
      return SetHandleErrorAndReturnLocked(impl, ENGINE_RESULT_INVALID_STATE,
                                           "engine is already destroyed");
    }
    if (g_runtime_startup_active && g_runtime_startup_owner == handle) {
      return SetHandleErrorAndReturnLocked(
          impl, ENGINE_RESULT_INVALID_STATE, "engine startup is already running");
    }
    if (g_runtime_active && g_runtime_owner == handle) {
      SetStartupState(impl, ENGINE_STARTUP_STATE_SUCCEEDED);
      ClearHandleErrorLocked(impl);
      SetThreadError(nullptr);
      return ENGINE_RESULT_OK;
    }
    if (g_runtime_active && g_runtime_owner != handle) {
      return SetHandleErrorAndReturnLocked(
          impl,
          ENGINE_RESULT_INVALID_STATE,
          "runtime is already active on another engine handle");
    }
    if (g_runtime_started_once) {
      return SetHandleErrorAndReturnLocked(
          impl,
          ENGINE_RESULT_NOT_SUPPORTED,
          "runtime restart is not supported yet; restart process to open another game");
    }

    stale_worker = DetachStartupWorker(impl);
    ResetStartupState(impl);
    SetStartupState(impl, ENGINE_STARTUP_STATE_RUNNING);
    PushStartupLog(impl, "engine_open_game_async: queued startup");
    MarkStartupWorkerRunning(impl, true);
    g_runtime_startup_active = true;
    g_runtime_startup_owner = handle;

    try {
      impl->startup.worker =
          std::thread([handle, impl, game_root_copy]() mutable {
            RunOpenGameAsync(handle, impl, std::move(game_root_copy));
          });
    } catch (...) {
      MarkStartupWorkerRunning(impl, false);
      SetStartupState(impl, ENGINE_STARTUP_STATE_FAILED);
      g_runtime_startup_active = false;
      g_runtime_startup_owner = nullptr;
      return SetHandleErrorAndReturnLocked(
          impl, ENGINE_RESULT_INTERNAL_ERROR, "failed to create startup thread");
    }

    ClearHandleErrorLocked(impl);
  }
  if (stale_worker.joinable()) {
    stale_worker.join();
  }
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_get_startup_state(engine_handle_t handle,
                                         uint32_t* out_state) {
  if (out_state == nullptr) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "out_state is null");
  }

  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  engine_handle_s* impl = nullptr;
  auto result = ValidateHandleLocked(handle, &impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }
  std::lock_guard<std::recursive_mutex> guard(impl->mutex);
  result = ValidateHandleThreadLocked(impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  *out_state = GetStartupState(impl);
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_drain_startup_logs(engine_handle_t handle,
                                          char* out_buffer,
                                          uint32_t buffer_size,
                                          uint32_t* out_bytes_written) {
  if (out_bytes_written == nullptr) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "out_bytes_written is null");
  }
  if (out_buffer == nullptr || buffer_size == 0) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "out_buffer is null or buffer_size is 0");
  }

  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  engine_handle_s* impl = nullptr;
  auto result = ValidateHandleLocked(handle, &impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }
  std::lock_guard<std::recursive_mutex> guard(impl->mutex);
  result = ValidateHandleThreadLocked(impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  uint32_t written = 0;
  {
    std::lock_guard<std::mutex> startup_guard(impl->startup.mutex);
    while (!impl->startup.logs.empty()) {
      const std::string line = impl->startup.logs.front();
      const uint32_t needed = static_cast<uint32_t>(line.size() + 1u);
      if (written + needed > buffer_size) {
        break;
      }
      std::memcpy(out_buffer + written, line.data(), line.size());
      written += static_cast<uint32_t>(line.size());
      out_buffer[written++] = '\n';
      impl->startup.logs.pop_front();
    }
  }
  if (written < buffer_size) {
    out_buffer[written] = '\0';
  } else {
    out_buffer[buffer_size - 1] = '\0';
  }
  *out_bytes_written = written;
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_tick(engine_handle_t handle, uint32_t delta_ms) {
  (void)delta_ms;

  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  engine_handle_s* impl = nullptr;
  auto result = ValidateHandleLocked(handle, &impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  std::lock_guard<std::recursive_mutex> guard(impl->mutex);
  result = ValidateHandleThreadLocked(impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }
  if (g_runtime_startup_active && g_runtime_startup_owner == handle) {
    return SetHandleErrorAndReturnLocked(
        impl, ENGINE_RESULT_INVALID_STATE, "engine startup is still running");
  }

  if (!g_runtime_active || g_runtime_owner != handle) {
    return SetHandleErrorAndReturnLocked(
        impl,
        ENGINE_RESULT_INVALID_STATE,
        "engine_open_game must succeed before engine_tick");
  }

  if (impl->state == ToStateValue(EngineState::kPaused)) {
    return SetHandleErrorAndReturnLocked(impl, ENGINE_RESULT_INVALID_STATE,
                                         "engine is paused");
  }

  if (impl->state != ToStateValue(EngineState::kOpened)) {
    return SetHandleErrorAndReturnLocked(impl, ENGINE_RESULT_INVALID_STATE,
                                         "engine is not in opened state");
  }
  impl->tick_count += 1;

  while (!impl->input.pending_events.empty()) {
    const engine_input_event_t queued_event = impl->input.pending_events.front();
    impl->input.pending_events.pop_front();

    const char* dispatch_error = nullptr;
    const engine_result_t dispatch_result =
        DispatchInputEventNow(impl, queued_event, &dispatch_error);
    if (dispatch_result != ENGINE_RESULT_OK) {
      return SetHandleErrorAndReturnLocked(
          impl, dispatch_result,
          dispatch_error != nullptr ? dispatch_error : "input dispatch failed");
    }
  }

#if defined(__ANDROID__)
  // Auto-attach pending ANativeWindow from JNI bridge.
  // The Kotlin plugin calls nativeSetSurface() which stores the
  // ANativeWindow in a global variable. Here we detect it and
  // attach it as the EGL WindowSurface render target so that
  // eglSwapBuffers delivers frames to Flutter's SurfaceTexture.
  if (!impl->render.native_window_attached) {
    ANativeWindow* pending_window = krkr_GetNativeWindow();
    if (pending_window) {
      uint32_t win_w = 0, win_h = 0;
      krkr_GetSurfaceDimensions(&win_w, &win_h);
      auto& egl = krkr::GetEngineEGLContext();
      if (win_w > 0 && win_h > 0) {
        bool attached = false;
        if (!egl.IsValid()) {
          // EGL context not yet initialized — use InitializeWithWindow
          // to create EGL display + context + WindowSurface in one step,
          // bypassing Pbuffer which may not be supported on this device.
          AndroidInfoLog("engine_tick: EGL not valid, InitializeWithWindow %ux%u", win_w, win_h);
          if (egl.InitializeWithWindow(pending_window, win_w, win_h, impl->render.angle_backend)) {
            attached = true;
            AndroidInfoLog("engine_tick: InitializeWithWindow success");
          } else {
            AndroidInfoLog("engine_tick: InitializeWithWindow failed");
          }
        } else {
          // EGL already initialized (Pbuffer) — attach WindowSurface
          AndroidInfoLog("engine_tick: EGL valid, AttachNativeWindow %ux%u", win_w, win_h);
          if (egl.AttachNativeWindow(pending_window, win_w, win_h)) {
            attached = true;
            AndroidInfoLog("engine_tick: AttachNativeWindow success");
          } else {
            AndroidInfoLog("engine_tick: AttachNativeWindow failed");
          }
        }
        if (attached) {
          impl->render.native_window_attached = true;
          spdlog::info("engine_tick: auto-attached ANativeWindow {}x{}", win_w, win_h);
          AndroidInfoLog("engine_tick: auto-attached ANativeWindow %ux%u", win_w, win_h);
          // Update window size for the draw device
          if (TVPMainWindow) {
            auto* dd = TVPMainWindow->GetDrawDevice();
            if (dd) {
              dd->SetWindowSize(static_cast<tjs_int>(win_w),
                                static_cast<tjs_int>(win_h));
            }
          }
        }
      } else if (impl->tick_count % 120 == 0) {
        AndroidInfoLog("engine_tick: pending ANativeWindow but size is 0 (%ux%u)",
                       win_w, win_h);
      }
      // Release the ref acquired by krkr_GetNativeWindow()
      ANativeWindow_release(pending_window);
    } else if (impl->tick_count % 180 == 0) {
      AndroidInfoLog("engine_tick: waiting for ANativeWindow (tick=%llu)",
                     static_cast<unsigned long long>(impl->tick_count));
    }
  } else {
    // Already attached — check if the JNI side has detached the window.
    ANativeWindow* current_window = krkr_GetNativeWindow();
    if (current_window) {
      ANativeWindow_release(current_window);
    } else {
      // Window was detached from JNI side — revert to Pbuffer
      auto& egl = krkr::GetEngineEGLContext();
      egl.DetachNativeWindow();
      impl->render.native_window_attached = false;
      spdlog::info("engine_tick: ANativeWindow detached, reverted to Pbuffer mode");
      AndroidInfoLog("engine_tick: ANativeWindow detached -> Pbuffer");
    }
  }
#endif

  if (TVPTerminated) {
    std::string except_msg = TVPEngineApi_GetGlobalException();
    if (!except_msg.empty()) {
      return SetHandleErrorAndReturnLocked(
          impl, ENGINE_RESULT_INTERNAL_ERROR, except_msg.c_str());
    } else {
      return SetHandleErrorAndReturnLocked(
          impl, ENGINE_RESULT_INVALID_STATE, "runtime has been terminated");
    }
  }

  // Frame rate limiting: when fps_limit > 0, skip rendering if not enough
  // time has elapsed since the last rendered frame. Input events above are
  // always processed regardless of the limit.
  if (impl->fps.limit > 0) {
    const auto now = std::chrono::steady_clock::now();
    if (impl->fps.initialized) {
      const auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(
          now - impl->fps.last_render_time).count();
      if (static_cast<uint64_t>(elapsed_us) < impl->fps.interval_us) {
        // Not yet time for next frame — skip rendering
        impl->frame.rendered_this_tick = false;
        ClearHandleErrorLocked(impl);
        SetThreadError(nullptr);
        return ENGINE_RESULT_OK;
      }
      // Advance the deadline by exactly one frame interval instead of
      // snapping to `now`. This eliminates the cumulative drift that
      // occurs when vsync intervals don't evenly divide the target
      // frame interval (e.g. 60 Hz vsync vs 30 fps target: 16.6 ms
      // does not divide 33.3 ms evenly, causing every other frame to
      // wait an extra vsync and dropping to ~20-24 fps).
      //
      // If we've fallen behind by more than one full interval (e.g.
      // the app was suspended), snap to `now` to avoid a burst of
      // catch-up renders.
      const auto ideal_next = impl->fps.last_render_time +
          std::chrono::microseconds(impl->fps.interval_us);
      if (now - ideal_next > std::chrono::microseconds(impl->fps.interval_us)) {
        // Fallen too far behind — reset to now
        impl->fps.last_render_time = now;
      } else {
        impl->fps.last_render_time = ideal_next;
      }
    } else {
      impl->fps.last_render_time = now;
      impl->fps.initialized = true;
    }
  }

  // Async startup may have made the EGL context current on a worker thread.
  // Ensure the owner/tick thread has a current context before any GL work.
  {
    auto& egl = krkr::GetEngineEGLContext();
    if (egl.IsValid() && !egl.MakeCurrent()) {
      return SetHandleErrorAndReturnLocked(
          impl,
          ENGINE_RESULT_INVALID_STATE,
          "failed to make EGL context current before engine_tick");
    }
  }

  // Drive one full frame (scene update + render + swap). In host mode
  // we must call Application->Run() which processes messages, triggers
  // scene composition, and invokes BasicDrawDevice::Show() →
  // form->UpdateDrawBuffer() — the actual rendering path.
  // TVPDrawSceneOnce() only restores GL state and calls SwapBuffer,
  // which is insufficient.
  if (::Application) {
    ::Application->Run();
  }
  ::TVPDrawSceneOnce(0);

  // Process deferred texture deletions. iTVPTexture2D::Release() uses
  // delayed deletion — textures are queued in _toDeleteTextures and only
  // freed when RecycleProcess() is called. Without this, every texture
  // released during the frame (via Independ/SetSize/Recreate) accumulates
  // indefinitely, causing a memory leak — especially visible in OpenGL
  // mode where each texture also holds GPU resources.
  iTVPTexture2D::RecycleProcess();

  if (TVPTerminated) {
    return SetHandleErrorAndReturnLocked(
        impl,
        ENGINE_RESULT_INVALID_STATE,
        "runtime requested termination");
  }

  // Mark that a frame was rendered this tick (for IOSurface mode notification)
  impl->frame.rendered_this_tick = true;

  // In IOSurface mode, the engine renders directly to the shared IOSurface
  // via the FBO — no need for glReadPixels. Skip the expensive readback.
  if (impl->render.native_window_attached) {
    // Android WindowSurface mode — TVPForceSwapBuffer() (called by
    // TVPDrawSceneOnce above) already performed eglSwapBuffers to deliver
    // the frame to Flutter's SurfaceTexture. Just update frame tracking.
    impl->frame.serial += 1;
    impl->frame.ready = true;
  } else if (!impl->render.iosurface_attached) {
    // Legacy Pbuffer readback path (slow, for backward compatibility)
    const FrameReadbackLayout layout = GetFrameReadbackLayoutLocked(impl);
    const size_t required_size =
        static_cast<size_t>(layout.stride_bytes) *
        static_cast<size_t>(layout.height);
    if (impl->frame.rgba.size() != required_size) {
      impl->frame.rgba.assign(required_size, 0);
    }

    if (required_size > 0 &&
        ReadCurrentFrameRgba(layout, impl->frame.rgba.data())) {
      impl->frame.width = layout.width;
      impl->frame.height = layout.height;
      impl->frame.stride_bytes = layout.stride_bytes;
      impl->frame.ready = true;
      impl->frame.serial += 1;
    } else if (!impl->frame.ready && required_size > 0) {
      std::fill(impl->frame.rgba.begin(), impl->frame.rgba.end(), 0);
      impl->frame.width = layout.width;
      impl->frame.height = layout.height;
      impl->frame.stride_bytes = layout.stride_bytes;
      impl->frame.ready = true;
      impl->frame.serial += 1;
    }
  } else {
    // IOSurface mode — just increment frame serial, no readback needed.
    // The render output is already in the shared IOSurface.
    glFlush(); // Ensure GPU commands are submitted
    impl->frame.serial += 1;
    impl->frame.ready = true;
  }

  ClearHandleErrorLocked(impl);
  SetThreadError(nullptr);
#if defined(__ANDROID__)
  if (impl->tick_count % 120 == 0) {
    AndroidInfoLog("engine_tick: tick=%llu rendered=%d serial=%llu native_window=%d iosurface=%d frame_ready=%d",
                   static_cast<unsigned long long>(impl->tick_count),
                   impl->frame.rendered_this_tick ? 1 : 0,
                   static_cast<unsigned long long>(impl->frame.serial),
                   impl->render.native_window_attached ? 1 : 0,
                   impl->render.iosurface_attached ? 1 : 0,
                   impl->frame.ready ? 1 : 0);
  }
#endif
  return ENGINE_RESULT_OK;
}

engine_result_t engine_pause(engine_handle_t handle) {
  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  engine_handle_s* impl = nullptr;
  auto result = ValidateHandleLocked(handle, &impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  std::lock_guard<std::recursive_mutex> guard(impl->mutex);
  result = ValidateHandleThreadLocked(impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  if (!g_runtime_active || g_runtime_owner != handle) {
    return SetHandleErrorAndReturnLocked(
        impl,
        ENGINE_RESULT_INVALID_STATE,
        "engine_open_game must succeed before engine_pause");
  }

  if (impl->state == ToStateValue(EngineState::kPaused)) {
    ClearHandleErrorLocked(impl);
    SetThreadError(nullptr);
    return ENGINE_RESULT_OK;
  }

  if (impl->state != ToStateValue(EngineState::kOpened)) {
    return SetHandleErrorAndReturnLocked(impl, ENGINE_RESULT_INVALID_STATE,
                                         "engine_pause requires opened state");
  }

  Application->OnDeactivate();
  impl->input.active_pointer_ids.clear();
  impl->input.pending_events.clear();
  impl->state = ToStateValue(EngineState::kPaused);
  ClearHandleErrorLocked(impl);
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_resume(engine_handle_t handle) {
  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  engine_handle_s* impl = nullptr;
  auto result = ValidateHandleLocked(handle, &impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  std::lock_guard<std::recursive_mutex> guard(impl->mutex);
  result = ValidateHandleThreadLocked(impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  if (!g_runtime_active || g_runtime_owner != handle) {
    return SetHandleErrorAndReturnLocked(
        impl,
        ENGINE_RESULT_INVALID_STATE,
        "engine_open_game must succeed before engine_resume");
  }

  if (impl->state == ToStateValue(EngineState::kOpened)) {
    ClearHandleErrorLocked(impl);
    SetThreadError(nullptr);
    return ENGINE_RESULT_OK;
  }

  if (impl->state != ToStateValue(EngineState::kPaused)) {
    return SetHandleErrorAndReturnLocked(impl, ENGINE_RESULT_INVALID_STATE,
                                         "engine_resume requires paused state");
  }

  Application->OnActivate();
  impl->state = ToStateValue(EngineState::kOpened);
  ClearHandleErrorLocked(impl);
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_set_option(engine_handle_t handle,
                                  const engine_option_t* option) {
  if (option == nullptr || option->key_utf8 == nullptr || option->key_utf8[0] == '\0') {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "option and option->key_utf8 must be non-null/non-empty");
  }
  if (option->value_utf8 == nullptr) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "option->value_utf8 must be non-null");
  }

  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  engine_handle_s* impl = nullptr;
  auto result = ValidateHandleLocked(handle, &impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  std::lock_guard<std::recursive_mutex> guard(impl->mutex);
  result = ValidateHandleThreadLocked(impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  // Handle fps_limit option: controls C++ side frame rate throttling
  const std::string key(option->key_utf8);
  if (key == ENGINE_OPTION_FPS_LIMIT) {
    const int fps = std::atoi(option->value_utf8);
    impl->fps.limit = fps > 0 ? static_cast<uint32_t>(fps) : 0;
    impl->fps.interval_us = fps > 0 ? (1000000u / static_cast<uint32_t>(fps)) : 0;
    // Reset timing so the next tick renders immediately
    impl->fps.initialized = false;
    spdlog::info("engine_set_option: fps_limit={} (interval={}us)",
                 impl->fps.limit, impl->fps.interval_us);
    ClearHandleErrorLocked(impl);
    SetThreadError(nullptr);
    return ENGINE_RESULT_OK;
  }

  // Handle angle_backend option: controls ANGLE EGL backend (Android only)
  if (key == ENGINE_OPTION_ANGLE_BACKEND) {
    if (g_engine_bootstrapped) {
      spdlog::warn("engine_set_option: angle_backend changed after engine initialization, "
                   "restart required to apply new backend");
    }
    const std::string val(option->value_utf8);
    if (val == ENGINE_ANGLE_BACKEND_VULKAN) {
      impl->render.angle_backend = krkr::AngleBackend::Vulkan;
    } else {
      impl->render.angle_backend = krkr::AngleBackend::OpenGLES;
    }
    spdlog::info("engine_set_option: angle_backend={}", option->value_utf8);
    ClearHandleErrorLocked(impl);
    SetThreadError(nullptr);
    return ENGINE_RESULT_OK;
  }

  // Handle plugin_trace option: enable/disable plugin call tracing
  if (key == ENGINE_OPTION_PLUGIN_TRACE) {
    const std::string v(option->value_utf8);
    const bool enabled = (v == "1" || v == "true");
    PluginCallTracer::Instance().SetEnabled(enabled);
    spdlog::info("engine_set_option: plugin_trace={}", enabled);
    // Also store in command line so TVPLoadInternalPlugins can read it
    TVPSetCommandLine(ttstr(option->key_utf8).c_str(), ttstr(option->value_utf8));
    ClearHandleErrorLocked(impl);
    SetThreadError(nullptr);
    return ENGINE_RESULT_OK;
  }

  // Handle mock_enabled option: enable/disable runtime mock bypass
  if (key == ENGINE_OPTION_MOCK_ENABLED) {
    const std::string v(option->value_utf8);
    const bool enabled = (v == "1" || v == "true");
    TJS::TVPSetMockEnabled(enabled);
    spdlog::info("engine_set_option: mock_enabled={}", enabled);
    TVPSetCommandLine(ttstr(option->key_utf8).c_str(), ttstr(option->value_utf8));
    ClearHandleErrorLocked(impl);
    SetThreadError(nullptr);
    return ENGINE_RESULT_OK;
  }

  // Handle console_log_file option: enable/disable krkr.console.log file output
  if (key == ENGINE_OPTION_CONSOLE_LOG_FILE) {
    const std::string v(option->value_utf8);
    const bool enabled = (v == "1" || v == "true");
    g_ConsoleLogFileEnabled = enabled;
    spdlog::info("engine_set_option: console_log_file={}", enabled);
    TVPSetCommandLine(ttstr(option->key_utf8).c_str(), ttstr(option->value_utf8));
    ClearHandleErrorLocked(impl);
    SetThreadError(nullptr);
    return ENGINE_RESULT_OK;
  }

  TVPSetCommandLine(ttstr(option->key_utf8).c_str(), ttstr(option->value_utf8));

  if (key == ENGINE_OPTION_ARCHIVE_CACHE_COUNT) {
    const int count = std::atoi(option->value_utf8);
    if (g_engine_bootstrapped && count > 0) {
      TVPSetArchiveCacheCount(static_cast<tjs_uint>(count));
    }
  } else if (key == ENGINE_OPTION_AUTOPATH_CACHE_COUNT) {
    const int count = std::atoi(option->value_utf8);
    if (g_engine_bootstrapped && count > 0) {
      TVPSetAutoPathCacheMaxCount(static_cast<tjs_uint>(count));
    }
  } else if (key == ENGINE_OPTION_PSB_CACHE_MB) {
    const int cache_mb = std::atoi(option->value_utf8);
    if (cache_mb > 0) {
      impl->memory_options.psb_cache_mb = cache_mb;
    }
    if (g_engine_bootstrapped && impl->memory_options.psb_cache_entries > 0 &&
        impl->memory_options.psb_cache_mb > 0) {
      PSB::SetPSBMediaCacheBudget(
          static_cast<size_t>(impl->memory_options.psb_cache_entries),
          static_cast<size_t>(impl->memory_options.psb_cache_mb) * 1024ULL *
              1024ULL);
    }
  } else if (key == ENGINE_OPTION_PSB_CACHE_ENTRIES) {
    const int entries = std::atoi(option->value_utf8);
    if (entries > 0) {
      impl->memory_options.psb_cache_entries = entries;
    }
    if (g_engine_bootstrapped && impl->memory_options.psb_cache_entries > 0 &&
        impl->memory_options.psb_cache_mb > 0) {
      PSB::SetPSBMediaCacheBudget(
          static_cast<size_t>(impl->memory_options.psb_cache_entries),
          static_cast<size_t>(impl->memory_options.psb_cache_mb) * 1024ULL *
              1024ULL);
    }
  }

  ClearHandleErrorLocked(impl);
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_set_surface_size(engine_handle_t handle,
                                        uint32_t width,
                                        uint32_t height) {
  if (width == 0 || height == 0) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "width and height must be > 0");
  }

  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  engine_handle_s* impl = nullptr;
  auto result = ValidateHandleLocked(handle, &impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  std::lock_guard<std::recursive_mutex> guard(impl->mutex);
  result = ValidateHandleThreadLocked(impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  if (impl->state == ToStateValue(EngineState::kDestroyed)) {
    return SetHandleErrorAndReturnLocked(impl, ENGINE_RESULT_INVALID_STATE,
                                         "engine is already destroyed");
  }

  impl->frame.surface_width = width;
  impl->frame.surface_height = height;
  impl->frame.width = 0;
  impl->frame.height = 0;
  impl->frame.stride_bytes = 0;
  impl->frame.rgba.clear();
  impl->frame.ready = false;

  // Propagate the new surface size to the EGL context and viewport.
  if (g_runtime_active && g_runtime_owner == handle) {
    auto& egl = krkr::GetEngineEGLContext();
    if (egl.IsValid()) {
      if (egl.HasNativeWindow()) {
        // Android WindowSurface mode: setDefaultBufferSize() already
        // changed the SurfaceTexture buffer dimensions. The EGL surface
        // auto-adapts on next eglSwapBuffers. Update our stored
        // dimensions so UpdateDrawBuffer() uses the correct viewport.
        egl.UpdateNativeWindowSize(width, height);
      } else {
        // Pbuffer mode (macOS / iOS): resize the Pbuffer surface.
        const uint32_t cur_w = egl.GetWidth();
        const uint32_t cur_h = egl.GetHeight();
        if (cur_w != width || cur_h != height) {
          egl.Resize(width, height);
          glViewport(0, 0, static_cast<GLsizei>(width),
                     static_cast<GLsizei>(height));
        }
      }
    }

    // Only update WindowSize here — DestRect is exclusively managed by
    // UpdateDrawBuffer() which calculates the correct letterbox viewport.
    if (TVPMainWindow) {
      auto* dd = TVPMainWindow->GetDrawDevice();
      if (dd) {
        dd->SetWindowSize(static_cast<tjs_int>(width),
                          static_cast<tjs_int>(height));
      }
    }
  }

  ClearHandleErrorLocked(impl);
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_get_frame_desc(engine_handle_t handle,
                                      engine_frame_desc_t* out_frame_desc) {
  if (out_frame_desc == nullptr) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "out_frame_desc is null");
  }
  if (out_frame_desc->struct_size < sizeof(engine_frame_desc_t)) {
    return SetThreadErrorAndReturn(
        ENGINE_RESULT_INVALID_ARGUMENT,
        "engine_frame_desc_t.struct_size is too small");
  }

  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  engine_handle_s* impl = nullptr;
  auto result = ValidateHandleLocked(handle, &impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  std::lock_guard<std::recursive_mutex> guard(impl->mutex);
  result = ValidateHandleThreadLocked(impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  if (impl->state == ToStateValue(EngineState::kDestroyed)) {
    return SetHandleErrorAndReturnLocked(impl, ENGINE_RESULT_INVALID_STATE,
                                         "engine is already destroyed");
  }

  FrameReadbackLayout layout;
  if (impl->frame.ready && impl->frame.width > 0 && impl->frame.height > 0 &&
      impl->frame.stride_bytes > 0) {
    layout.width = impl->frame.width;
    layout.height = impl->frame.height;
    layout.stride_bytes = impl->frame.stride_bytes;
  } else {
    layout = GetFrameReadbackLayoutLocked(impl);
  }

  std::memset(out_frame_desc, 0, sizeof(*out_frame_desc));
  out_frame_desc->struct_size = sizeof(engine_frame_desc_t);
  out_frame_desc->width = layout.width;
  out_frame_desc->height = layout.height;
  out_frame_desc->stride_bytes = layout.stride_bytes;
  out_frame_desc->pixel_format = ENGINE_PIXEL_FORMAT_RGBA8888;
  out_frame_desc->frame_serial = impl->frame.serial;

  ClearHandleErrorLocked(impl);
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_read_frame_rgba(engine_handle_t handle,
                                       void* out_pixels,
                                       size_t out_pixels_size) {
  if (out_pixels == nullptr) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "out_pixels is null");
  }

  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  engine_handle_s* impl = nullptr;
  auto result = ValidateHandleLocked(handle, &impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  std::lock_guard<std::recursive_mutex> guard(impl->mutex);
  result = ValidateHandleThreadLocked(impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  if (impl->state != ToStateValue(EngineState::kOpened) &&
      impl->state != ToStateValue(EngineState::kPaused)) {
    return SetHandleErrorAndReturnLocked(
        impl,
        ENGINE_RESULT_INVALID_STATE,
        "engine_open_game must succeed before engine_read_frame_rgba");
  }

  FrameReadbackLayout layout;
  if (impl->frame.ready && impl->frame.width > 0 && impl->frame.height > 0 &&
      impl->frame.stride_bytes > 0) {
    layout.width = impl->frame.width;
    layout.height = impl->frame.height;
    layout.stride_bytes = impl->frame.stride_bytes;
  } else {
    layout = GetFrameReadbackLayoutLocked(impl);
    const size_t required_size =
        static_cast<size_t>(layout.stride_bytes) *
        static_cast<size_t>(layout.height);
    impl->frame.rgba.assign(required_size, 0);
    impl->frame.width = layout.width;
    impl->frame.height = layout.height;
    impl->frame.stride_bytes = layout.stride_bytes;
    impl->frame.ready = true;
  }

  const size_t required_size =
      static_cast<size_t>(layout.stride_bytes) *
      static_cast<size_t>(layout.height);
  if (out_pixels_size < required_size) {
    return SetHandleErrorAndReturnLocked(
        impl,
        ENGINE_RESULT_INVALID_ARGUMENT,
        "out_pixels_size is smaller than required frame buffer size");
  }

  if (impl->frame.rgba.size() < required_size) {
    impl->frame.rgba.resize(required_size, 0);
  }
  std::memcpy(out_pixels, impl->frame.rgba.data(), required_size);

  ClearHandleErrorLocked(impl);
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_get_host_native_window(engine_handle_t handle,
                                              void** out_window_handle) {
  if (out_window_handle == nullptr) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "out_window_handle is null");
  }
  *out_window_handle = nullptr;

  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  engine_handle_s* impl = nullptr;
  auto result = ValidateHandleLocked(handle, &impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  std::lock_guard<std::recursive_mutex> guard(impl->mutex);
  result = ValidateHandleThreadLocked(impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  if (!g_runtime_active || g_runtime_owner != handle) {
    return SetHandleErrorAndReturnLocked(
        impl,
        ENGINE_RESULT_INVALID_STATE,
        "engine_open_game must succeed before engine_get_host_native_window");
  }

#if defined(TARGET_OS_MAC) && TARGET_OS_MAC && !TARGET_OS_IPHONE
  // No native GLFW window in ANGLE Pbuffer mode.
  return SetHandleErrorAndReturnLocked(
      impl,
      ENGINE_RESULT_NOT_SUPPORTED,
      "engine_get_host_native_window is not supported in headless ANGLE mode");
#else
  return SetHandleErrorAndReturnLocked(
      impl,
      ENGINE_RESULT_NOT_SUPPORTED,
      "engine_get_host_native_window is only supported on macOS runtime");
#endif
}

engine_result_t engine_get_host_native_view(engine_handle_t handle,
                                            void** out_view_handle) {
  if (out_view_handle == nullptr) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "out_view_handle is null");
  }
  *out_view_handle = nullptr;

  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  engine_handle_s* impl = nullptr;
  auto result = ValidateHandleLocked(handle, &impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  std::lock_guard<std::recursive_mutex> guard(impl->mutex);
  result = ValidateHandleThreadLocked(impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  if (!g_runtime_active || g_runtime_owner != handle) {
    return SetHandleErrorAndReturnLocked(
        impl,
        ENGINE_RESULT_INVALID_STATE,
        "engine_open_game must succeed before engine_get_host_native_view");
  }

  // No native GLFW window in ANGLE Pbuffer mode — native view is unavailable.
  return SetHandleErrorAndReturnLocked(
      impl,
      ENGINE_RESULT_NOT_SUPPORTED,
      "engine_get_host_native_view is not supported in headless ANGLE mode");
}

engine_result_t engine_send_input(engine_handle_t handle,
                                  const engine_input_event_t* event) {
  if (event == nullptr) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "event is null");
  }
  if (event->struct_size < sizeof(engine_input_event_t)) {
    return SetThreadErrorAndReturn(
        ENGINE_RESULT_INVALID_ARGUMENT,
        "engine_input_event_t.struct_size is too small");
  }

  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  engine_handle_s* impl = nullptr;
  auto result = ValidateHandleLocked(handle, &impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  std::lock_guard<std::recursive_mutex> guard(impl->mutex);
  result = ValidateHandleThreadLocked(impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  if (impl->state == ToStateValue(EngineState::kPaused)) {
    return SetHandleErrorAndReturnLocked(impl, ENGINE_RESULT_INVALID_STATE,
                                         "engine is paused");
  }
  if (impl->state != ToStateValue(EngineState::kOpened)) {
    return SetHandleErrorAndReturnLocked(
        impl,
        ENGINE_RESULT_INVALID_STATE,
        "engine_open_game must succeed before engine_send_input");
  }

  switch (event->type) {
    case ENGINE_INPUT_EVENT_POINTER_DOWN:
    case ENGINE_INPUT_EVENT_POINTER_MOVE:
    case ENGINE_INPUT_EVENT_POINTER_UP:
    case ENGINE_INPUT_EVENT_POINTER_SCROLL:
    case ENGINE_INPUT_EVENT_KEY_DOWN:
    case ENGINE_INPUT_EVENT_KEY_UP:
    case ENGINE_INPUT_EVENT_TEXT_INPUT:
    case ENGINE_INPUT_EVENT_BACK:
      break;
    default:
      return SetHandleErrorAndReturnLocked(impl, ENGINE_RESULT_NOT_SUPPORTED,
                                           "unsupported input event type");
  }

  if (event->type == ENGINE_INPUT_EVENT_POINTER_DOWN ||
      event->type == ENGINE_INPUT_EVENT_POINTER_MOVE ||
      event->type == ENGINE_INPUT_EVENT_POINTER_UP ||
      event->type == ENGINE_INPUT_EVENT_POINTER_SCROLL) {
    if (!IsFinitePointerValue(event->x) || !IsFinitePointerValue(event->y) ||
        !IsFinitePointerValue(event->delta_x) ||
        !IsFinitePointerValue(event->delta_y)) {
      return SetHandleErrorAndReturnLocked(
          impl, ENGINE_RESULT_INVALID_ARGUMENT,
          "pointer coordinates contain non-finite values");
    }
  }

  impl->input.pending_events.push_back(*event);
  constexpr size_t kMaxQueuedInputs = 512;
  if (impl->input.pending_events.size() > kMaxQueuedInputs) {
    impl->input.pending_events.pop_front();
  }

  ClearHandleErrorLocked(impl);
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_set_render_target_iosurface(engine_handle_t handle,
                                                    uint32_t iosurface_id,
                                                    uint32_t width,
                                                    uint32_t height) {
  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  engine_handle_s* impl = nullptr;
  auto result = ValidateHandleLocked(handle, &impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  std::lock_guard<std::recursive_mutex> guard(impl->mutex);
  result = ValidateHandleThreadLocked(impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  if (!g_runtime_active || g_runtime_owner != handle) {
    return SetHandleErrorAndReturnLocked(
        impl,
        ENGINE_RESULT_INVALID_STATE,
        "engine_open_game must succeed before engine_set_render_target_iosurface");
  }

#if defined(__APPLE__)
  auto& egl = krkr::GetEngineEGLContext();
  if (!egl.IsValid()) {
    return SetHandleErrorAndReturnLocked(
        impl,
        ENGINE_RESULT_INVALID_STATE,
        "EGL context not initialized");
  }

  if (iosurface_id == 0) {
    // Detach — revert to Pbuffer mode
    egl.DetachIOSurface();
    impl->render.iosurface_attached = false;
    spdlog::info("engine_set_render_target_iosurface: detached, Pbuffer mode");
  } else {
    if (width == 0 || height == 0) {
      return SetHandleErrorAndReturnLocked(
          impl,
          ENGINE_RESULT_INVALID_ARGUMENT,
          "width and height must be > 0 when setting IOSurface");
    }
    if (!egl.AttachIOSurface(iosurface_id, width, height)) {
      return SetHandleErrorAndReturnLocked(
          impl,
          ENGINE_RESULT_INTERNAL_ERROR,
          "failed to attach IOSurface as render target");
    }
    impl->render.iosurface_attached = true;
    spdlog::info("engine_set_render_target_iosurface: attached id={} {}x{}",
                 iosurface_id, width, height);

    // Only update WindowSize here — DestRect is exclusively managed by
    // UpdateDrawBuffer() which calculates the correct letterbox viewport.
    // Setting DestRect here would overwrite the viewport offset and cause
    // mouse Y-axis misalignment when game aspect ratio != surface aspect ratio.
    if (TVPMainWindow) {
      auto* dd = TVPMainWindow->GetDrawDevice();
      if (dd) {
        dd->SetWindowSize(static_cast<tjs_int>(width),
                          static_cast<tjs_int>(height));
      }
    }
  }

  ClearHandleErrorLocked(impl);
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
#else
  (void)iosurface_id;
  (void)width;
  (void)height;
  return SetHandleErrorAndReturnLocked(
      impl,
      ENGINE_RESULT_NOT_SUPPORTED,
      "IOSurface render target is only supported on macOS");
#endif
}

engine_result_t engine_set_render_target_surface(engine_handle_t handle,
                                                  void* native_window,
                                                  uint32_t width,
                                                  uint32_t height) {
  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  engine_handle_s* impl = nullptr;
  auto result = ValidateHandleLocked(handle, &impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  std::lock_guard<std::recursive_mutex> guard(impl->mutex);
  result = ValidateHandleThreadLocked(impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  if (!g_runtime_active || g_runtime_owner != handle) {
    return SetHandleErrorAndReturnLocked(
        impl,
        ENGINE_RESULT_INVALID_STATE,
        "engine_open_game must succeed before engine_set_render_target_surface");
  }

#if defined(__ANDROID__)
  auto& egl = krkr::GetEngineEGLContext();
  if (!egl.IsValid()) {
    return SetHandleErrorAndReturnLocked(
        impl,
        ENGINE_RESULT_INVALID_STATE,
        "EGL context not initialized");
  }

  if (native_window == nullptr) {
    // Detach — revert to Pbuffer mode
    egl.DetachNativeWindow();
    impl->render.native_window_attached = false;
    spdlog::info("engine_set_render_target_surface: detached, Pbuffer mode");
  } else {
    if (width == 0 || height == 0) {
      return SetHandleErrorAndReturnLocked(
          impl,
          ENGINE_RESULT_INVALID_ARGUMENT,
          "width and height must be > 0 when setting Surface");
    }
    if (!egl.AttachNativeWindow(native_window, width, height)) {
      return SetHandleErrorAndReturnLocked(
          impl,
          ENGINE_RESULT_INTERNAL_ERROR,
          "failed to attach Android Surface as render target");
    }
    impl->render.native_window_attached = true;
    spdlog::info("engine_set_render_target_surface: attached {}x{}", width, height);

    // Update window size for the draw device
    if (TVPMainWindow) {
      auto* dd = TVPMainWindow->GetDrawDevice();
      if (dd) {
        dd->SetWindowSize(static_cast<tjs_int>(width),
                          static_cast<tjs_int>(height));
      }
    }
  }

  ClearHandleErrorLocked(impl);
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
#else
  (void)native_window;
  (void)width;
  (void)height;
  return SetHandleErrorAndReturnLocked(
      impl,
      ENGINE_RESULT_NOT_SUPPORTED,
      "Surface render target is only supported on Android");
#endif
}

engine_result_t engine_get_frame_rendered_flag(engine_handle_t handle,
                                                uint32_t* out_flag) {
  if (out_flag == nullptr) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "out_flag is null");
  }

  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  engine_handle_s* impl = nullptr;
  auto result = ValidateHandleLocked(handle, &impl);
  if (result != ENGINE_RESULT_OK) {
    *out_flag = 0;
    return result;
  }

  std::lock_guard<std::recursive_mutex> guard(impl->mutex);
  *out_flag = impl->frame.rendered_this_tick ? 1 : 0;
  impl->frame.rendered_this_tick = false;

  ClearHandleErrorLocked(impl);
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_get_renderer_info(engine_handle_t handle,
                                         char* out_buffer,
                                         uint32_t buffer_size) {
  if (out_buffer == nullptr || buffer_size == 0) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "out_buffer is null or buffer_size is 0");
  }
  out_buffer[0] = '\0';

  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  engine_handle_s* impl = nullptr;
  auto result = ValidateHandleLocked(handle, &impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  std::lock_guard<std::recursive_mutex> guard(impl->mutex);
  result = ValidateHandleThreadLocked(impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  if (!g_runtime_active || g_runtime_owner != handle) {
    return SetHandleErrorAndReturnLocked(
        impl,
        ENGINE_RESULT_INVALID_STATE,
        "engine_open_game must succeed before engine_get_renderer_info");
  }

  auto& egl = krkr::GetEngineEGLContext();
  if (!egl.IsValid()) {
    return SetHandleErrorAndReturnLocked(
        impl,
        ENGINE_RESULT_INVALID_STATE,
        "EGL context not initialized");
  }
  if (!egl.MakeCurrent()) {
    return SetHandleErrorAndReturnLocked(
        impl,
        ENGINE_RESULT_INVALID_STATE,
        "failed to make EGL context current");
  }

  // Query GL renderer and version strings from the active ANGLE context.
  const char* gl_renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
  const char* gl_version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
  if (!gl_renderer) gl_renderer = "(unknown)";
  if (!gl_version) gl_version = "(unknown)";

  // Build a combined info string.
  std::string info = std::string(gl_renderer) + " | " + std::string(gl_version);
  std::strncpy(out_buffer, info.c_str(), buffer_size - 1);
  out_buffer[buffer_size - 1] = '\0';

  ClearHandleErrorLocked(impl);
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_get_memory_stats(engine_handle_t handle,
                                        engine_memory_stats_t* out_stats) {
  if (out_stats == nullptr) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "out_stats is null");
  }
  if (out_stats->struct_size < sizeof(engine_memory_stats_t)) {
    return SetThreadErrorAndReturn(
        ENGINE_RESULT_INVALID_ARGUMENT,
        "engine_memory_stats_t.struct_size is too small");
  }

  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  engine_handle_s* impl = nullptr;
  auto result = ValidateHandleLocked(handle, &impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  std::lock_guard<std::recursive_mutex> guard(impl->mutex);
  result = ValidateHandleThreadLocked(impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  std::memset(out_stats, 0, sizeof(*out_stats));
  out_stats->struct_size = sizeof(engine_memory_stats_t);
  TVPMemoryInfo meminfo{};
  TVPGetMemoryInfo(meminfo);
  out_stats->self_used_mb = static_cast<uint32_t>(
      std::max<tjs_int>(0, TVPGetSelfUsedMemory()));
  out_stats->system_free_mb = static_cast<uint32_t>(
      std::max<tjs_int>(0, TVPGetSystemFreeMemory()));
  out_stats->system_total_mb = static_cast<uint32_t>(meminfo.MemTotal / 1024);

  out_stats->graphic_cache_bytes = TVPGetGraphicCacheTotalBytes();
  out_stats->graphic_cache_limit_bytes = TVPGetGraphicCacheLimit();
  out_stats->xp3_segment_cache_bytes = TVPGetXP3SegmentCacheTotalBytes();

  out_stats->archive_cache_entries = TVPGetArchiveCacheCount();
  out_stats->archive_cache_limit = TVPGetArchiveCacheLimit();
  out_stats->autopath_cache_entries = TVPGetAutoPathCacheCount();
  out_stats->autopath_cache_limit = TVPGetAutoPathCacheLimit();
  out_stats->autopath_table_entries = TVPGetAutoPathTableCount();

  PSB::PSBMediaCacheStats psb_stats{};
  if (PSB::GetPSBMediaCacheStats(psb_stats)) {
    out_stats->psb_cache_bytes = psb_stats.bytesInUse;
    out_stats->psb_cache_entries = static_cast<uint32_t>(psb_stats.entryCount);
    out_stats->psb_cache_entry_limit =
        static_cast<uint32_t>(psb_stats.entryLimit);
    out_stats->psb_cache_hits = psb_stats.hitCount;
    out_stats->psb_cache_misses = psb_stats.missCount;
  }

  ClearHandleErrorLocked(impl);
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

const char* engine_get_last_error(engine_handle_t handle) {
  if (handle == nullptr) {
    return g_thread_error.c_str();
  }

  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  if (!IsHandleLiveLocked(handle)) {
    SetThreadError("engine handle is invalid or already destroyed");
    return g_thread_error.c_str();
  }
  auto* impl = reinterpret_cast<engine_handle_s*>(handle);
  std::lock_guard<std::recursive_mutex> guard(impl->mutex);
  return impl->last_error.c_str();
}

}  // extern "C"

#else

#include <cstring>
#include <deque>
#include <new>
#include <string>
#include <unordered_set>
#include <mutex>

struct engine_handle_s {
  std::recursive_mutex mutex;
  std::string last_error;
  int state = 0;
  uint32_t surface_width = 1280;
  uint32_t surface_height = 720;
  uint64_t frame_serial = 0;
  uint32_t startup_state = ENGINE_STARTUP_STATE_IDLE;
  std::deque<std::string> startup_logs;
};

namespace {

enum class EngineState {
  kCreated = 0,
  kOpened,
  kPaused,
  kDestroyed,
};

inline int ToStateValue(EngineState state) {
  return static_cast<int>(state);
}

std::recursive_mutex g_registry_mutex;
std::unordered_set<engine_handle_t> g_live_handles;
thread_local std::string g_thread_error;

void SetThreadError(const char* message) {
  g_thread_error = (message != nullptr) ? message : "";
}

engine_result_t SetThreadErrorAndReturn(engine_result_t result,
                                        const char* message) {
  SetThreadError(message);
  return result;
}

bool IsHandleLiveLocked(engine_handle_t handle) {
  return g_live_handles.find(handle) != g_live_handles.end();
}

engine_result_t ValidateHandleLocked(engine_handle_t handle,
                                     engine_handle_s** out_impl) {
  if (handle == nullptr) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "engine handle is null");
  }
  if (!IsHandleLiveLocked(handle)) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "engine handle is invalid or already destroyed");
  }
  *out_impl = reinterpret_cast<engine_handle_s*>(handle);
  return ENGINE_RESULT_OK;
}

void SetHandleErrorLocked(engine_handle_s* impl, const char* message) {
  impl->last_error = (message != nullptr) ? message : "";
}

}  // namespace

extern "C" {

engine_result_t engine_get_runtime_api_version(uint32_t* out_api_version) {
  if (out_api_version == nullptr) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "out_api_version is null");
  }
  *out_api_version = ENGINE_API_VERSION;
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_create(const engine_create_desc_t* desc,
                              engine_handle_t* out_handle) {
  if (desc == nullptr || out_handle == nullptr) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "engine_create requires non-null desc and out_handle");
  }

  if (desc->struct_size < sizeof(engine_create_desc_t)) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "engine_create_desc_t.struct_size is too small");
  }

  const uint32_t expected_major = (ENGINE_API_VERSION >> 24u) & 0xFFu;
  const uint32_t caller_major = (desc->api_version >> 24u) & 0xFFu;
  if (caller_major != expected_major) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_NOT_SUPPORTED,
                                   "unsupported engine API major version");
  }

  auto* impl = new (std::nothrow) engine_handle_s();
  if (impl == nullptr) {
    *out_handle = nullptr;
    return SetThreadErrorAndReturn(ENGINE_RESULT_INTERNAL_ERROR,
                                   "failed to allocate engine handle");
  }
  impl->state = ToStateValue(EngineState::kCreated);

  auto handle = reinterpret_cast<engine_handle_t>(impl);
  {
    std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
    g_live_handles.insert(handle);
  }

  *out_handle = handle;
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_destroy(engine_handle_t handle) {
  if (handle == nullptr) {
    SetThreadError(nullptr);
    return ENGINE_RESULT_OK;
  }

  engine_handle_s* impl = nullptr;
  {
    std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
    auto it = g_live_handles.find(handle);
    if (it == g_live_handles.end()) {
      return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                     "engine handle is invalid or already destroyed");
    }
    impl = reinterpret_cast<engine_handle_s*>(handle);
    g_live_handles.erase(it);
  }

  {
    std::lock_guard<std::recursive_mutex> guard(impl->mutex);
    impl->state = ToStateValue(EngineState::kDestroyed);
    impl->last_error.clear();
  }
  delete impl;
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_open_game(engine_handle_t handle,
                                 const char* game_root_path_utf8,
                                 const char* startup_script_utf8) {
  (void)startup_script_utf8;

  if (game_root_path_utf8 == nullptr || game_root_path_utf8[0] == '\0') {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "game_root_path_utf8 is null or empty");
  }

  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  engine_handle_s* impl = nullptr;
  auto result = ValidateHandleLocked(handle, &impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  std::lock_guard<std::recursive_mutex> guard(impl->mutex);
  if (impl->state == ToStateValue(EngineState::kDestroyed)) {
    SetHandleErrorLocked(impl, "engine is already destroyed");
    return ENGINE_RESULT_INVALID_STATE;
  }

  impl->state = ToStateValue(EngineState::kOpened);
  impl->startup_state = ENGINE_STARTUP_STATE_SUCCEEDED;
  impl->startup_logs.clear();
  impl->startup_logs.push_back("engine_open_game => OK");
  impl->last_error.clear();
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_open_game_async(engine_handle_t handle,
                                       const char* game_root_path_utf8,
                                       const char* startup_script_utf8) {
  return engine_open_game(handle, game_root_path_utf8, startup_script_utf8);
}

engine_result_t engine_get_startup_state(engine_handle_t handle,
                                         uint32_t* out_state) {
  if (out_state == nullptr) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "out_state is null");
  }

  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  engine_handle_s* impl = nullptr;
  auto result = ValidateHandleLocked(handle, &impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }
  std::lock_guard<std::recursive_mutex> guard(impl->mutex);
  *out_state = impl->startup_state;
  impl->last_error.clear();
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_drain_startup_logs(engine_handle_t handle,
                                          char* out_buffer,
                                          uint32_t buffer_size,
                                          uint32_t* out_bytes_written) {
  if (out_bytes_written == nullptr) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "out_bytes_written is null");
  }
  if (out_buffer == nullptr || buffer_size == 0) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "out_buffer is null or buffer_size is 0");
  }

  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  engine_handle_s* impl = nullptr;
  auto result = ValidateHandleLocked(handle, &impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  std::lock_guard<std::recursive_mutex> guard(impl->mutex);
  uint32_t written = 0;
  while (!impl->startup_logs.empty()) {
    const std::string line = impl->startup_logs.front();
    const uint32_t needed = static_cast<uint32_t>(line.size() + 1u);
    if (written + needed > buffer_size) {
      break;
    }
    std::memcpy(out_buffer + written, line.data(), line.size());
    written += static_cast<uint32_t>(line.size());
    out_buffer[written++] = '\n';
    impl->startup_logs.pop_front();
  }
  if (written < buffer_size) {
    out_buffer[written] = '\0';
  } else {
    out_buffer[buffer_size - 1] = '\0';
  }
  *out_bytes_written = written;
  impl->last_error.clear();
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_tick(engine_handle_t handle, uint32_t delta_ms) {
  (void)delta_ms;

  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  engine_handle_s* impl = nullptr;
  auto result = ValidateHandleLocked(handle, &impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  std::lock_guard<std::recursive_mutex> guard(impl->mutex);
  if (impl->state == ToStateValue(EngineState::kPaused)) {
    SetHandleErrorLocked(impl, "engine is paused");
    return ENGINE_RESULT_INVALID_STATE;
  }
  if (impl->state != ToStateValue(EngineState::kOpened)) {
    SetHandleErrorLocked(impl, "engine_open_game must succeed before engine_tick");
    return ENGINE_RESULT_INVALID_STATE;
  }

  impl->frame_serial += 1;
  impl->last_error.clear();
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_pause(engine_handle_t handle) {
  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  engine_handle_s* impl = nullptr;
  auto result = ValidateHandleLocked(handle, &impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  std::lock_guard<std::recursive_mutex> guard(impl->mutex);
  if (impl->state == ToStateValue(EngineState::kPaused)) {
    impl->last_error.clear();
    SetThreadError(nullptr);
    return ENGINE_RESULT_OK;
  }
  if (impl->state != ToStateValue(EngineState::kOpened)) {
    SetHandleErrorLocked(impl, "engine_pause requires opened state");
    return ENGINE_RESULT_INVALID_STATE;
  }

  impl->state = ToStateValue(EngineState::kPaused);
  impl->last_error.clear();
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_resume(engine_handle_t handle) {
  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  engine_handle_s* impl = nullptr;
  auto result = ValidateHandleLocked(handle, &impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  std::lock_guard<std::recursive_mutex> guard(impl->mutex);
  if (impl->state == ToStateValue(EngineState::kOpened)) {
    impl->last_error.clear();
    SetThreadError(nullptr);
    return ENGINE_RESULT_OK;
  }
  if (impl->state != ToStateValue(EngineState::kPaused)) {
    SetHandleErrorLocked(impl, "engine_resume requires paused state");
    return ENGINE_RESULT_INVALID_STATE;
  }

  impl->state = ToStateValue(EngineState::kOpened);
  impl->last_error.clear();
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_set_option(engine_handle_t handle,
                                  const engine_option_t* option) {
  if (option == nullptr || option->key_utf8 == nullptr || option->key_utf8[0] == '\0') {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "option and option->key_utf8 must be non-null/non-empty");
  }
  if (option->value_utf8 == nullptr) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "option->value_utf8 must be non-null");
  }

  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  engine_handle_s* impl = nullptr;
  auto result = ValidateHandleLocked(handle, &impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  std::lock_guard<std::recursive_mutex> guard(impl->mutex);
  if (impl->state == ToStateValue(EngineState::kDestroyed)) {
    SetHandleErrorLocked(impl, "engine is already destroyed");
    return ENGINE_RESULT_INVALID_STATE;
  }

  impl->last_error.clear();
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_set_surface_size(engine_handle_t handle,
                                        uint32_t width,
                                        uint32_t height) {
  if (width == 0 || height == 0) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "width and height must be > 0");
  }

  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  engine_handle_s* impl = nullptr;
  auto result = ValidateHandleLocked(handle, &impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  std::lock_guard<std::recursive_mutex> guard(impl->mutex);
  if (impl->state == ToStateValue(EngineState::kDestroyed)) {
    SetHandleErrorLocked(impl, "engine is already destroyed");
    return ENGINE_RESULT_INVALID_STATE;
  }

  impl->surface_width = width;
  impl->surface_height = height;
  impl->last_error.clear();
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_get_frame_desc(engine_handle_t handle,
                                      engine_frame_desc_t* out_frame_desc) {
  if (out_frame_desc == nullptr) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "out_frame_desc is null");
  }
  if (out_frame_desc->struct_size < sizeof(engine_frame_desc_t)) {
    return SetThreadErrorAndReturn(
        ENGINE_RESULT_INVALID_ARGUMENT,
        "engine_frame_desc_t.struct_size is too small");
  }

  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  engine_handle_s* impl = nullptr;
  auto result = ValidateHandleLocked(handle, &impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  std::lock_guard<std::recursive_mutex> guard(impl->mutex);
  if (impl->state == ToStateValue(EngineState::kDestroyed)) {
    SetHandleErrorLocked(impl, "engine is already destroyed");
    return ENGINE_RESULT_INVALID_STATE;
  }

  std::memset(out_frame_desc, 0, sizeof(*out_frame_desc));
  out_frame_desc->struct_size = sizeof(engine_frame_desc_t);
  out_frame_desc->width = impl->surface_width;
  out_frame_desc->height = impl->surface_height;
  out_frame_desc->stride_bytes = impl->surface_width * 4u;
  out_frame_desc->pixel_format = ENGINE_PIXEL_FORMAT_RGBA8888;
  out_frame_desc->frame_serial = impl->frame_serial;

  impl->last_error.clear();
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_read_frame_rgba(engine_handle_t handle,
                                       void* out_pixels,
                                       size_t out_pixels_size) {
  if (out_pixels == nullptr) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "out_pixels is null");
  }

  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  engine_handle_s* impl = nullptr;
  auto result = ValidateHandleLocked(handle, &impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  std::lock_guard<std::recursive_mutex> guard(impl->mutex);
  if (impl->state != ToStateValue(EngineState::kOpened) &&
      impl->state != ToStateValue(EngineState::kPaused)) {
    SetHandleErrorLocked(impl,
                         "engine_open_game must succeed before engine_read_frame_rgba");
    return ENGINE_RESULT_INVALID_STATE;
  }

  const size_t required_size =
      static_cast<size_t>(impl->surface_width) *
      static_cast<size_t>(impl->surface_height) * 4u;
  if (out_pixels_size < required_size) {
    SetHandleErrorLocked(
        impl,
        "out_pixels_size is smaller than required frame buffer size");
    return ENGINE_RESULT_INVALID_ARGUMENT;
  }

  std::memset(out_pixels, 0, required_size);
  impl->last_error.clear();
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_get_host_native_window(engine_handle_t handle,
                                              void** out_window_handle) {
  if (out_window_handle == nullptr) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "out_window_handle is null");
  }
  *out_window_handle = nullptr;

  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  engine_handle_s* impl = nullptr;
  auto result = ValidateHandleLocked(handle, &impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  std::lock_guard<std::recursive_mutex> guard(impl->mutex);
  if (impl->state != ToStateValue(EngineState::kOpened) &&
      impl->state != ToStateValue(EngineState::kPaused)) {
    SetHandleErrorLocked(
        impl,
        "engine_open_game must succeed before engine_get_host_native_window");
    return ENGINE_RESULT_INVALID_STATE;
  }

  SetHandleErrorLocked(impl,
                       "engine_get_host_native_window is not supported");
  return ENGINE_RESULT_NOT_SUPPORTED;
}

engine_result_t engine_get_host_native_view(engine_handle_t handle,
                                            void** out_view_handle) {
  if (out_view_handle == nullptr) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "out_view_handle is null");
  }
  *out_view_handle = nullptr;

  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  engine_handle_s* impl = nullptr;
  auto result = ValidateHandleLocked(handle, &impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  std::lock_guard<std::recursive_mutex> guard(impl->mutex);
  if (impl->state != ToStateValue(EngineState::kOpened) &&
      impl->state != ToStateValue(EngineState::kPaused)) {
    SetHandleErrorLocked(
        impl,
        "engine_open_game must succeed before engine_get_host_native_view");
    return ENGINE_RESULT_INVALID_STATE;
  }

  SetHandleErrorLocked(impl, "engine_get_host_native_view is not supported");
  return ENGINE_RESULT_NOT_SUPPORTED;
}

engine_result_t engine_send_input(engine_handle_t handle,
                                  const engine_input_event_t* event) {
  if (event == nullptr) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "event is null");
  }
  if (event->struct_size < sizeof(engine_input_event_t)) {
    return SetThreadErrorAndReturn(
        ENGINE_RESULT_INVALID_ARGUMENT,
        "engine_input_event_t.struct_size is too small");
  }

  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  engine_handle_s* impl = nullptr;
  auto result = ValidateHandleLocked(handle, &impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  std::lock_guard<std::recursive_mutex> guard(impl->mutex);
  if (impl->state == ToStateValue(EngineState::kPaused)) {
    SetHandleErrorLocked(impl, "engine is paused");
    return ENGINE_RESULT_INVALID_STATE;
  }
  if (impl->state != ToStateValue(EngineState::kOpened)) {
    SetHandleErrorLocked(impl,
                         "engine_open_game must succeed before engine_send_input");
    return ENGINE_RESULT_INVALID_STATE;
  }

  switch (event->type) {
    case ENGINE_INPUT_EVENT_POINTER_DOWN:
    case ENGINE_INPUT_EVENT_POINTER_MOVE:
    case ENGINE_INPUT_EVENT_POINTER_UP:
    case ENGINE_INPUT_EVENT_POINTER_SCROLL:
    case ENGINE_INPUT_EVENT_KEY_DOWN:
    case ENGINE_INPUT_EVENT_KEY_UP:
    case ENGINE_INPUT_EVENT_TEXT_INPUT:
    case ENGINE_INPUT_EVENT_BACK:
      break;
    default:
      SetHandleErrorLocked(impl, "unsupported input event type");
      return ENGINE_RESULT_NOT_SUPPORTED;
  }

  impl->last_error.clear();
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_set_render_target_iosurface(engine_handle_t handle,
                                                    uint32_t iosurface_id,
                                                    uint32_t width,
                                                    uint32_t height) {
  (void)iosurface_id;
  (void)width;
  (void)height;

  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  engine_handle_s* impl = nullptr;
  auto result = ValidateHandleLocked(handle, &impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  SetHandleErrorLocked(impl,
                       "engine_set_render_target_iosurface is not supported in stub build");
  return ENGINE_RESULT_NOT_SUPPORTED;
}

engine_result_t engine_set_render_target_surface(engine_handle_t handle,
                                                  void* native_window,
                                                  uint32_t width,
                                                  uint32_t height) {
  (void)native_window;
  (void)width;
  (void)height;

  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  engine_handle_s* impl = nullptr;
  auto result = ValidateHandleLocked(handle, &impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  SetHandleErrorLocked(impl,
                       "engine_set_render_target_surface is not supported in stub build");
  return ENGINE_RESULT_NOT_SUPPORTED;
}

engine_result_t engine_get_frame_rendered_flag(engine_handle_t handle,
                                                uint32_t* out_flag) {
  if (out_flag == nullptr) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "out_flag is null");
  }
  *out_flag = 0;

  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  engine_handle_s* impl = nullptr;
  auto result = ValidateHandleLocked(handle, &impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  impl->last_error.clear();
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_get_renderer_info(engine_handle_t handle,
                                         char* out_buffer,
                                         uint32_t buffer_size) {
  (void)handle;
  if (out_buffer == nullptr || buffer_size == 0) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "out_buffer is null or buffer_size is 0");
  }
  out_buffer[0] = '\0';

  // Stub build — return a placeholder string.
  const char* stub_info = "Stub (no runtime)";
  std::strncpy(out_buffer, stub_info, buffer_size - 1);
  out_buffer[buffer_size - 1] = '\0';
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

engine_result_t engine_get_memory_stats(engine_handle_t handle,
                                        engine_memory_stats_t* out_stats) {
  if (out_stats == nullptr) {
    return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                   "out_stats is null");
  }
  if (out_stats->struct_size < sizeof(engine_memory_stats_t)) {
    return SetThreadErrorAndReturn(
        ENGINE_RESULT_INVALID_ARGUMENT,
        "engine_memory_stats_t.struct_size is too small");
  }

  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  engine_handle_s* impl = nullptr;
  auto result = ValidateHandleLocked(handle, &impl);
  if (result != ENGINE_RESULT_OK) {
    return result;
  }

  std::memset(out_stats, 0, sizeof(*out_stats));
  out_stats->struct_size = sizeof(engine_memory_stats_t);
  impl->last_error.clear();
  SetThreadError(nullptr);
  return ENGINE_RESULT_OK;
}

const char* engine_get_last_error(engine_handle_t handle) {
  if (handle == nullptr) {
    return g_thread_error.c_str();
  }

  std::lock_guard<std::recursive_mutex> registry_guard(g_registry_mutex);
  if (!IsHandleLiveLocked(handle)) {
    SetThreadError("engine handle is invalid or already destroyed");
    return g_thread_error.c_str();
  }
  auto* impl = reinterpret_cast<engine_handle_s*>(handle);
  std::lock_guard<std::recursive_mutex> guard(impl->mutex);
  return impl->last_error.c_str();
}

}  // extern "C"

#endif
