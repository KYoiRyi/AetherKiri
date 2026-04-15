/// Centralized SharedPreferences keys and engine option constants.
///
/// All pages (home_page, settings_page, game_page) should reference
/// these constants instead of defining their own copies.
class PrefsKeys {
  PrefsKeys._(); // Prevent instantiation

  // ── SharedPreferences keys ──────────────────────────────────────
  static const String dylibPath = 'krkr2_dylib_path';
  static const String engineMode = 'krkr2_engine_mode';
  static const String perfOverlay = 'krkr2_perf_overlay';
  static const String fpsLimitEnabled = 'krkr2_fps_limit_enabled';
  static const String targetFps = 'krkr2_target_fps';
  static const String renderer = 'krkr2_renderer';
  static const String angleBackend = 'krkr2_angle_backend';
  static const String locale = 'krkr2_locale';
  static const String themeMode = 'krkr2_theme_mode';
  static const String forceLandscape = 'krkr2_force_landscape';
  static const String pluginTrace = 'krkr2_plugin_trace';
  static const String mockEnabled = 'krkr2_mock_enabled';
  static const String consoleLogFile = 'krkr2_console_log_file';
  static const String traceLog = 'krkr2_trace_log';
  static const String exportScripts = 'krkr2_export_scripts';

  /// Pending play session: JSON { "path": "...", "startTime": "ISO8601" }. Cleared on normal exit or applied on next launch.
  static const String pendingPlaySession = 'krkr2_pending_play_session';

  /// Recently settled play session IDs. Used to avoid duplicate crediting.
  static const String settledPlaySessionIds = 'krkr2_settled_play_session_ids';

  // ── Engine option keys (must match C++ ENGINE_OPTION_* constants) ─
  static const String optionAngleBackend = 'angle_backend';
  static const String optionFpsLimit = 'fps_limit';
  static const String optionRenderer = 'renderer';
  static const String optionMemoryProfile = 'memory_profile';
  static const String optionMemoryBudgetMb = 'memory_budget_mb';
  static const String optionMemoryLogIntervalMs = 'memory_log_interval_ms';
  static const String optionPsbCacheMb = 'psb_cache_mb';
  static const String optionPsbCacheEntries = 'psb_cache_entries';
  static const String optionArchiveCacheCount = 'archive_cache_count';
  static const String optionAutoPathCacheCount = 'autopath_cache_count';
  static const String optionPluginTrace = 'plugin_trace';
  static const String optionMockEnabled = 'mock_enabled';
  static const String optionConsoleLogFile = 'console_log_file';
  static const String optionTraceLog = 'trace_log';
  static const String optionExportScripts = 'export_scripts';

  // ── Engine option values ────────────────────────────────────────
  static const String angleBackendGles = 'gles';
  static const String angleBackendVulkan = 'vulkan';
  static const String rendererOpengl = 'opengl';
  static const String rendererSoftware = 'software';
  static const String memoryProfileBalanced = 'balanced';
  static const String memoryProfileAggressive = 'aggressive';

  // ── Engine mode values ──────────────────────────────────────────
  static const String engineModeBuiltIn = 'builtIn';
  static const String engineModeCustom = 'custom';

  // ── Default values ──────────────────────────────────────────────
  static const int defaultFps = 60;
  static const List<int> fpsOptions = [30, 60, 120];
}
