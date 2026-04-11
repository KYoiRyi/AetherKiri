// ignore: unused_import
import 'package:intl/intl.dart' as intl;
import 'app_localizations.dart';

// ignore_for_file: type=lint

/// The translations for English (`en`).
class AppLocalizationsEn extends AppLocalizations {
  AppLocalizationsEn([String locale = 'en']) : super(locale);

  @override
  String get appTitle => 'AetherKiri';

  @override
  String get settings => 'Settings';

  @override
  String get addGame => 'Add Game';

  @override
  String get refresh => 'Refresh';

  @override
  String get howToImport => 'How to Import';

  @override
  String get noGamesYet => 'No games added yet';

  @override
  String get noGamesHintDesktop =>
      'Click \"Add Game\" to select a game directory';

  @override
  String get noGamesHintIos =>
      'Use the Files app to copy game folders to:\nOn My iPhone > Krkr2 > Games\nThen tap \"Refresh\"';

  @override
  String get importGames => 'Import Games';

  @override
  String get importGamesDesc =>
      'Please copy your game folders to this app\'s directory using the Files app:';

  @override
  String get importStep1 => '1. Open the \"Files\" app on your iPhone';

  @override
  String get importStep2 => '2. Go to: On My iPhone > Krkr2 > Games';

  @override
  String get importStep3 => '3. Copy your game folder into the Games directory';

  @override
  String get importStep4 =>
      '4. Come back and tap \"Refresh\" to detect new games';

  @override
  String get gamesDirectory => 'Games directory: Games/';

  @override
  String get gotIt => 'Got it';

  @override
  String get removeGame => 'Remove Game';

  @override
  String removeGameConfirm(String title) {
    return 'Remove \"$title\" from the list?\nThis will NOT delete the game files.';
  }

  @override
  String get cancel => 'Cancel';

  @override
  String get remove => 'Remove';

  @override
  String get renameGame => 'Rename Game';

  @override
  String get displayTitle => 'Display Title';

  @override
  String get save => 'Save';

  @override
  String gameAlreadyExists(String title) {
    return 'Game already exists: $title';
  }

  @override
  String get builtInReady => 'Built-in ✓';

  @override
  String get builtInNotReady => 'Built-in ✗';

  @override
  String get customNotSet => 'Custom (not set)';

  @override
  String get engineNotFoundBuiltIn =>
      'Built-in engine not found. Please use the build script to bundle the engine, or switch to Custom mode in Settings.';

  @override
  String get engineNotFoundCustom =>
      'Engine dylib not set. Please configure it in Settings first.';

  @override
  String lastPlayed(String time) {
    return 'Last played: $time';
  }

  @override
  String playDuration(String duration) {
    return 'Played $duration';
  }

  @override
  String get rename => 'Rename';

  @override
  String get setCover => 'Set Cover';

  @override
  String get coverFromGallery => 'Choose from Gallery';

  @override
  String get coverFromCamera => 'Take Photo';

  @override
  String get coverRemove => 'Remove Cover';

  @override
  String get settingsEngine => 'Engine';

  @override
  String get engineMode => 'Engine Mode';

  @override
  String get builtIn => 'Built-in';

  @override
  String get custom => 'Custom';

  @override
  String get builtInEngineAvailable => 'Built-in engine available';

  @override
  String get builtInEngineNotFound => 'Built-in engine not found';

  @override
  String get builtInEngineHint =>
      'Use the build script to compile and bundle the engine into the app.';

  @override
  String get engineDylibPath => 'Engine dylib path';

  @override
  String get notSetRequired => 'Not set (required)';

  @override
  String get clearPath => 'Clear path';

  @override
  String get browse => 'Browse...';

  @override
  String get selectEngineDylib => 'Select Engine dylib';

  @override
  String get settingsRendering => 'Rendering';

  @override
  String get settingsDevelopment => 'Development';

  @override
  String get renderPipeline => 'Render Pipeline';

  @override
  String get renderPipelineHint =>
      'Requires restarting the game to take effect';

  @override
  String get opengl => 'OpenGL';

  @override
  String get software => 'Software';

  @override
  String get graphicsBackend => 'Graphics Backend';

  @override
  String get graphicsBackendHint =>
      'ANGLE translation layer backend (Android only). Requires restart.';

  @override
  String get opengles => 'OpenGL ES';

  @override
  String get vulkan => 'Vulkan';

  @override
  String get performanceOverlay => 'Performance Overlay';

  @override
  String get performanceOverlayDesc => 'Show FPS and graphics API info';

  @override
  String get fpsLimitEnabled => 'Frame Rate Limit';

  @override
  String get fpsLimitEnabledDesc =>
      'Limit engine rendering frequency to save power';

  @override
  String get fpsLimitOff => 'Off (VSync)';

  @override
  String get forceLandscape => 'Lock Landscape';

  @override
  String get forceLandscapeDesc =>
      'Force landscape orientation when running games (recommended for phones)';

  @override
  String get pluginTrace => 'Plugin Trace Log';

  @override
  String get pluginTraceDesc =>
      'Record all plugin native calls to plugin_trace.log for debugging';

  @override
  String get mockBypass => 'Mock Bypass';

  @override
  String get mockBypassDesc =>
      'Suppress errors from missing plugins by returning mock objects. Disable to expose real errors for debugging.';

  @override
  String get consoleLogFile => 'Console Log File';

  @override
  String get consoleLogFileDesc =>
      'Write engine console log to krkr.console.log file';

  @override
  String get targetFrameRate => 'Target Frame Rate';

  @override
  String get targetFrameRateDesc =>
      'Maximum rendering frequency when limit is enabled';

  @override
  String fpsLabel(int fps) {
    return '$fps FPS';
  }

  @override
  String get settingsGeneral => 'General';

  @override
  String get language => 'Language';

  @override
  String get languageSystem => 'System Default';

  @override
  String get languageEn => 'English';

  @override
  String get languageZh => '简体中文';

  @override
  String get languageJa => '日本語';

  @override
  String get themeMode => 'Theme';

  @override
  String get themeDark => 'Dark';

  @override
  String get themeLight => 'Light';

  @override
  String get themeSystem => 'System';

  @override
  String get settingsAbout => 'About';

  @override
  String get version => 'Version';

  @override
  String get aboutVersionDesc => '0.2.0-beta.1';

  @override
  String get aboutAuthor => 'Author';

  @override
  String get aboutAuthorName => 'reAAAq (Adapted by KYoiRyi)';

  @override
  String get aboutGithubFork => 'AetherKiri (Current Fork)';

  @override
  String get aboutEmail => 'Email';

  @override
  String get aboutEmailCopied => 'Email copied to clipboard';

  @override
  String get gameEngineError => 'Engine Error';

  @override
  String get unknownError => 'Unknown error';

  @override
  String get back => 'Back';

  @override
  String get retry => 'Retry';

  @override
  String get hideDebug => 'Hide Debug';

  @override
  String get showDebug => 'Show Debug';

  @override
  String get pause => 'Pause';

  @override
  String get resume => 'Resume';

  @override
  String get exitGame => 'Exit Game';

  @override
  String get selectGameDirectory => 'Select Game Directory';

  @override
  String get selectGameArchive => 'Select XP3 Archive';

  @override
  String get addArchive => 'Add XP3';

  @override
  String get justNow => 'just now';

  @override
  String minutesAgo(int count) {
    return '$count min ago';
  }

  @override
  String hoursAgo(int count) {
    return '$count hours ago';
  }

  @override
  String daysAgo(int count) {
    return '$count days ago';
  }

  @override
  String get packXp3 => 'Pack as XP3';

  @override
  String get unpackXp3 => 'Unpack XP3';

  @override
  String get packingProgress => 'Packing...';

  @override
  String get unpackingProgress => 'Unpacking...';

  @override
  String get packComplete => 'Packed successfully';

  @override
  String get unpackComplete => 'Unpacked successfully';

  @override
  String xp3OperationFailed(String error) {
    return 'Operation failed: $error';
  }

  @override
  String get launchGame => 'Launch Game';

  @override
  String get copiedToClipboard => 'Copied to clipboard';

  @override
  String get gameFormat => 'Format';

  @override
  String get gamePath => 'Path';

  @override
  String get scrapeMetadata => 'Scrape info';

  @override
  String get scrapeMetadataDialogTitle => 'Scrape info';

  @override
  String get scrapeMetadataSearchHint => 'Enter game name to search';

  @override
  String get scrapeMetadataSearch => 'Search';

  @override
  String get scrapeMetadataSelectTitle => 'Select matching game';

  @override
  String get scrapeMetadataNoResults =>
      'No matching games. Try another keyword.';

  @override
  String get scrapeMetadataConfirm => 'Confirm';

  @override
  String get scrapeMetadataSuccess => 'Name and cover updated.';

  @override
  String get scrapeMetadataCoverFailed =>
      'Name updated. Cover failed; you can set it manually.';

  @override
  String get scrapeMetadataEnterName => 'Please enter a game name.';

  @override
  String get scrapeMetadataSourceError =>
      'Source unavailable. Try again later.';

  @override
  String get scrapeMetadataSelectOne => 'Please select a game.';

  @override
  String get scrapeAfterAddPrompt =>
      'Scrape this game? Choose Yes to search and fill in name and cover.';

  @override
  String get scrapeAfterAddNo => 'No';

  @override
  String get scrapeAfterAddYes => 'Yes';
}
