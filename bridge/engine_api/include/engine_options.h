/**
 * @file engine_options.h
 * @brief Well-known engine option key/value constants.
 *
 * These constants unify the option strings used across the C++ codebase
 * (engine_api, EngineBootstrap) to avoid typos and ensure consistency.
 */
#ifndef KRKR2_ENGINE_OPTIONS_H_
#define KRKR2_ENGINE_OPTIONS_H_

/* ── Option Keys ────────────────────────────────────────────────── */

/** ANGLE EGL backend selection (Android only; other platforms ignore). */
#define ENGINE_OPTION_ANGLE_BACKEND       "angle_backend"

/** Frame rate limit (0 = unlimited / follow vsync). */
#define ENGINE_OPTION_FPS_LIMIT           "fps_limit"

/** Render pipeline selection ("opengl" or "software"). */
#define ENGINE_OPTION_RENDERER            "renderer"

/** Memory profile ("balanced" / "aggressive").
 *  Consumed by the C++ memory governor via TVPGetCommandLine(). */
#define ENGINE_OPTION_MEMORY_PROFILE      "memory_profile"

/** Runtime memory budget in MB (0 = auto).
 *  Consumed by the C++ memory governor via TVPGetCommandLine(). */
#define ENGINE_OPTION_MEMORY_BUDGET_MB    "memory_budget_mb"

/** Memory governor log interval in milliseconds.
 *  Consumed by the C++ memory governor via TVPGetCommandLine(). */
#define ENGINE_OPTION_MEMORY_LOG_INTERVAL_MS "memory_log_interval_ms"

/** PSB resource cache budget in MB. */
#define ENGINE_OPTION_PSB_CACHE_MB        "psb_cache_mb"

/** PSB resource cache max entry count. */
#define ENGINE_OPTION_PSB_CACHE_ENTRIES   "psb_cache_entries"

/** Archive cache max entry count. */
#define ENGINE_OPTION_ARCHIVE_CACHE_COUNT "archive_cache_count"

/** Auto path cache max entry count. */
#define ENGINE_OPTION_AUTOPATH_CACHE_COUNT "autopath_cache_count"

/** Enable plugin call tracing to plugin_trace.log ("0"/"1"). */
#define ENGINE_OPTION_PLUGIN_TRACE "plugin_trace"

/** Enable/disable runtime mock bypass ("0"/"1", default "1").
 *  When disabled, missing plugins/classes cause real errors instead of
 *  being silently absorbed by mock objects. Useful for debugging. */
#define ENGINE_OPTION_MOCK_ENABLED "mock_enabled"

/* ── ANGLE Backend Values ───────────────────────────────────────── */

/** Use ANGLE's OpenGL ES backend (default). */
#define ENGINE_ANGLE_BACKEND_GLES         "gles"

/** Use ANGLE's Vulkan backend. */
#define ENGINE_ANGLE_BACKEND_VULKAN       "vulkan"

/* ── Renderer Values ────────────────────────────────────────────── */

#define ENGINE_RENDERER_OPENGL            "opengl"
#define ENGINE_RENDERER_SOFTWARE          "software"

#define ENGINE_MEMORY_PROFILE_BALANCED    "balanced"
#define ENGINE_MEMORY_PROFILE_AGGRESSIVE  "aggressive"

#endif  /* KRKR2_ENGINE_OPTIONS_H_ */
