#ifndef KRKR2_ENGINE_API_H_
#define KRKR2_ENGINE_API_H_

#include <stddef.h>
#include <stdint.h>

/* Export macro for shared-library builds. */
#if defined(_WIN32)
#if defined(ENGINE_API_BUILD_SHARED)
#define ENGINE_API_EXPORT __declspec(dllexport)
#elif defined(ENGINE_API_USE_SHARED)
#define ENGINE_API_EXPORT __declspec(dllimport)
#else
#define ENGINE_API_EXPORT
#endif
#else
#if defined(__GNUC__) && __GNUC__ >= 4
#define ENGINE_API_EXPORT __attribute__((visibility("default")))
#else
#define ENGINE_API_EXPORT
#endif
#endif

#if defined(__cplusplus)
extern "C" {
#endif

/* ABI version: major(8bit), minor(8bit), patch(16bit). */
#define ENGINE_API_VERSION 0x01000000u
#define ENGINE_API_MAKE_VERSION(major, minor, patch) \
  ((((uint32_t)(major)&0xFFu) << 24u) | (((uint32_t)(minor)&0xFFu) << 16u) | \
   ((uint32_t)(patch)&0xFFFFu))

typedef struct engine_handle_s* engine_handle_t;

typedef enum engine_result_t {
  ENGINE_RESULT_OK = 0,
  ENGINE_RESULT_INVALID_ARGUMENT = -1,
  ENGINE_RESULT_INVALID_STATE = -2,
  ENGINE_RESULT_NOT_SUPPORTED = -3,
  ENGINE_RESULT_IO_ERROR = -4,
  ENGINE_RESULT_INTERNAL_ERROR = -5
} engine_result_t;

typedef struct engine_create_desc_t {
  uint32_t struct_size;
  uint32_t api_version;
  const char* writable_path_utf8;
  const char* cache_path_utf8;
  void* user_data;
  uint64_t reserved_u64[4];
  void* reserved_ptr[4];
} engine_create_desc_t;

typedef struct engine_option_t {
  const char* key_utf8;
  const char* value_utf8;
  uint64_t reserved_u64[2];
  void* reserved_ptr[2];
} engine_option_t;

typedef enum engine_pixel_format_t {
  ENGINE_PIXEL_FORMAT_UNKNOWN = 0,
  ENGINE_PIXEL_FORMAT_RGBA8888 = 1
} engine_pixel_format_t;

typedef struct engine_frame_desc_t {
  uint32_t struct_size;
  uint32_t width;
  uint32_t height;
  uint32_t stride_bytes;
  uint32_t pixel_format;
  uint64_t frame_serial;
  uint64_t reserved_u64[4];
  void* reserved_ptr[4];
} engine_frame_desc_t;

typedef struct engine_memory_stats_t {
  uint32_t struct_size;
  uint32_t self_used_mb;
  uint32_t system_free_mb;
  uint32_t system_total_mb;

  uint64_t graphic_cache_bytes;
  uint64_t graphic_cache_limit_bytes;
  uint64_t xp3_segment_cache_bytes;

  uint64_t psb_cache_bytes;
  uint32_t psb_cache_entries;
  uint32_t psb_cache_entry_limit;
  uint64_t psb_cache_hits;
  uint64_t psb_cache_misses;

  uint32_t archive_cache_entries;
  uint32_t archive_cache_limit;
  uint32_t autopath_cache_entries;
  uint32_t autopath_cache_limit;
  uint32_t autopath_table_entries;
  uint32_t reserved_u32;

  uint64_t reserved_u64[4];
  void* reserved_ptr[4];
} engine_memory_stats_t;

typedef enum engine_input_event_type_t {
  ENGINE_INPUT_EVENT_POINTER_DOWN = 1,
  ENGINE_INPUT_EVENT_POINTER_MOVE = 2,
  ENGINE_INPUT_EVENT_POINTER_UP = 3,
  ENGINE_INPUT_EVENT_POINTER_SCROLL = 4,
  ENGINE_INPUT_EVENT_KEY_DOWN = 5,
  ENGINE_INPUT_EVENT_KEY_UP = 6,
  ENGINE_INPUT_EVENT_TEXT_INPUT = 7,
  ENGINE_INPUT_EVENT_BACK = 8
} engine_input_event_type_t;

typedef enum engine_startup_state_t {
  ENGINE_STARTUP_STATE_IDLE = 0,
  ENGINE_STARTUP_STATE_RUNNING = 1,
  ENGINE_STARTUP_STATE_SUCCEEDED = 2,
  ENGINE_STARTUP_STATE_FAILED = 3
} engine_startup_state_t;

typedef struct engine_input_event_t {
  uint32_t struct_size;
  uint32_t type;
  uint64_t timestamp_micros;
  double x;
  double y;
  double delta_x;
  double delta_y;
  int32_t pointer_id;
  int32_t button;
  int32_t key_code;
  int32_t modifiers;
  uint32_t unicode_codepoint;
  uint32_t reserved_u32;
  uint64_t reserved_u64[2];
  void* reserved_ptr[2];
} engine_input_event_t;

/*
 * Returns runtime API version in out_api_version.
 * out_api_version must be non-null.
 */
ENGINE_API_EXPORT engine_result_t engine_get_runtime_api_version(
    uint32_t* out_api_version);

/*
 * Creates an engine handle.
 * desc and out_handle must be non-null.
 * out_handle is set only when ENGINE_RESULT_OK is returned.
 */
ENGINE_API_EXPORT engine_result_t engine_create(const engine_create_desc_t* desc,
                                                engine_handle_t* out_handle);

/*
 * Destroys engine handle and releases all resources.
 * Idempotent: passing a null handle returns ENGINE_RESULT_OK.
 */
ENGINE_API_EXPORT engine_result_t engine_destroy(engine_handle_t handle);

/*
 * Opens a game package/root directory.
 * handle and game_root_path_utf8 must be non-null.
 * startup_script_utf8 may be null to use default startup script.
 */
ENGINE_API_EXPORT engine_result_t engine_open_game(
    engine_handle_t handle, const char* game_root_path_utf8,
    const char* startup_script_utf8);

/*
 * Starts game opening asynchronously on a background worker.
 * Returns immediately when the startup task is scheduled.
 */
ENGINE_API_EXPORT engine_result_t engine_open_game_async(
    engine_handle_t handle, const char* game_root_path_utf8,
    const char* startup_script_utf8);

/*
 * Gets async startup state.
 * out_state must be non-null.
 */
ENGINE_API_EXPORT engine_result_t engine_get_startup_state(
    engine_handle_t handle, uint32_t* out_state);

/*
 * Drains startup logs into caller buffer as UTF-8 text.
 * Each log line is terminated by '\n'.
 * Returns bytes written in out_bytes_written.
 */
ENGINE_API_EXPORT engine_result_t engine_drain_startup_logs(
    engine_handle_t handle, char* out_buffer, uint32_t buffer_size,
    uint32_t* out_bytes_written);

/*
 * Ticks engine main loop once.
 * handle must be non-null.
 * delta_ms is caller-provided elapsed milliseconds.
 */
ENGINE_API_EXPORT engine_result_t engine_tick(engine_handle_t handle,
                                              uint32_t delta_ms);

/*
 * Pauses runtime execution.
 * Idempotent: calling pause on a paused engine returns ENGINE_RESULT_OK.
 */
ENGINE_API_EXPORT engine_result_t engine_pause(engine_handle_t handle);

/*
 * Resumes runtime execution.
 * Idempotent: calling resume on a running engine returns ENGINE_RESULT_OK.
 */
ENGINE_API_EXPORT engine_result_t engine_resume(engine_handle_t handle);

/*
 * Sets runtime option by UTF-8 key/value pair.
 * handle and option must be non-null.
 */
ENGINE_API_EXPORT engine_result_t engine_set_option(engine_handle_t handle,
                                                    const engine_option_t* option);

/*
 * Sets logical render surface size in pixels.
 * width and height must be greater than zero.
 */
ENGINE_API_EXPORT engine_result_t engine_set_surface_size(engine_handle_t handle,
                                                          uint32_t width,
                                                          uint32_t height);

/*
 * Gets current frame descriptor.
 * out_frame_desc->struct_size must be initialized by caller.
 */
ENGINE_API_EXPORT engine_result_t engine_get_frame_desc(
    engine_handle_t handle, engine_frame_desc_t* out_frame_desc);

/*
 * Reads current frame into caller-provided RGBA8888 buffer.
 * out_pixels_size must be >= stride_bytes * height from engine_get_frame_desc.
 */
ENGINE_API_EXPORT engine_result_t engine_read_frame_rgba(
    engine_handle_t handle, void* out_pixels, size_t out_pixels_size);

/*
 * Gets host-native render window handle.
 * On macOS runtime build this is NSWindow*.
 * Returns ENGINE_RESULT_NOT_SUPPORTED on unsupported platforms/builds.
 */
ENGINE_API_EXPORT engine_result_t engine_get_host_native_window(
    engine_handle_t handle, void** out_window_handle);

/*
 * Gets host-native render view handle.
 * On macOS runtime build this is NSView* (typically the GLFW content view).
 * Returns ENGINE_RESULT_NOT_SUPPORTED on unsupported platforms/builds.
 */
ENGINE_API_EXPORT engine_result_t engine_get_host_native_view(
    engine_handle_t handle, void** out_view_handle);

/*
 * Sends one input event to the runtime.
 * event->struct_size must be initialized by caller.
 */
ENGINE_API_EXPORT engine_result_t engine_send_input(engine_handle_t handle,
                                                    const engine_input_event_t* event);

/*
 * Exports the current main window menu tree as UTF-8 JSON.
 * Writes bytes written in out_bytes_written.
 * Returns an empty JSON array ("[]") when no menu is available.
 */
ENGINE_API_EXPORT engine_result_t engine_get_main_menu_json(
    engine_handle_t handle, char* out_buffer, uint32_t buffer_size,
    uint32_t* out_bytes_written);

/*
 * Activates one exported menu item by slash-separated index path
 * (for example "0/2/1").
 */
ENGINE_API_EXPORT engine_result_t engine_activate_menu_item(
    engine_handle_t handle, const char* item_path_utf8);

/*
 * Sets an IOSurface as the render target for the engine.
 * When set, engine_tick renders directly to this IOSurface (zero-copy),
 * bypassing the glReadPixels path used by engine_read_frame_rgba.
 *
 * iosurface_id: The IOSurfaceID obtained from IOSurfaceGetID().
 *               Pass 0 to detach and revert to the default Pbuffer mode.
 * width/height: Dimensions of the IOSurface in pixels.
 *
 * Platform: macOS only. Returns ENGINE_RESULT_NOT_SUPPORTED on other platforms.
 */
ENGINE_API_EXPORT engine_result_t engine_set_render_target_iosurface(
    engine_handle_t handle, uint32_t iosurface_id,
    uint32_t width, uint32_t height);

/*
 * Sets an Android Surface (from SurfaceTexture) as the render target.
 * When set, engine_tick renders to an EGL WindowSurface created from the
 * ANativeWindow. eglSwapBuffers() delivers frames directly to Flutter's
 * SurfaceTexture (GPU zero-copy).
 *
 * native_window: ANativeWindow* obtained from ANativeWindow_fromSurface().
 *                Pass NULL to detach and revert to the default Pbuffer mode.
 * width/height: Dimensions in pixels.
 *
 * Platform: Android only. Returns ENGINE_RESULT_NOT_SUPPORTED on other platforms.
 */
ENGINE_API_EXPORT engine_result_t engine_set_render_target_surface(
    engine_handle_t handle, void* native_window,
    uint32_t width, uint32_t height);

/*
 * Queries whether the last engine_tick produced a new rendered frame.
 * out_
 *
 * out_
 *   - 0: no new frame since last query
 *   - 1: a new frame was rendered
 *
 * This is useful in IOSurface mode to know when to call
 * textureFrameAvailable() on the Flutter side.
 */
ENGINE_API_EXPORT engine_result_t engine_get_frame_rendered_flag(
    engine_handle_t handle, uint32_t* out_
);

/*
 * Queries the graphics renderer information string.
 * Writes a null-terminated UTF-8 string into out_buffer describing
 * the active graphics backend (e.g. "Metal", "OpenGL ES", "D3D11").
 *
 * out_buffer and buffer_size must be non-null / > 0.
 * If the buffer is too small the string is truncated.
 * Returns ENGINE_RESULT_INVALID_STATE if the runtime is not active.
 */
ENGINE_API_EXPORT engine_result_t engine_get_renderer_info(
    engine_handle_t handle, char* out_buffer, uint32_t buffer_size);

/*
 * Gets runtime memory/cache statistics snapshot.
 * out_stats->struct_size must be initialized by caller.
 */
ENGINE_API_EXPORT engine_result_t engine_get_memory_stats(
    engine_handle_t handle, engine_memory_stats_t* out_stats);

/*
 * Returns last error message as UTF-8 null-terminated string.
 * The returned pointer remains valid until next API call on the same handle.
 * Returns empty string when no error is recorded.
 */
ENGINE_API_EXPORT const char* engine_get_last_error(engine_handle_t handle);

#if defined(__cplusplus)
}  /* extern "C" */
#endif

#endif  /* KRKR2_ENGINE_API_H_ */
