import 'dart:io';

import 'package:file_picker/file_picker.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:image_picker/image_picker.dart';
import 'package:path/path.dart' as p;
import 'package:path_provider/path_provider.dart';
import 'package:shared_preferences/shared_preferences.dart';

import '../l10n/app_localizations.dart';
import '../models/game_info.dart';
import '../services/game_manager.dart';
import '../theme/app_theme.dart';
import '../theme/app_animations.dart';
import '../utils/xp3_utils.dart';
import 'game_detail_page.dart';
import 'game_page.dart';
import 'settings_page.dart';
import '../constants/prefs_keys.dart';

/// Engine loading mode: built-in (bundled in .app) or custom (user-specified).
enum EngineMode { builtIn, custom }

/// The home / launcher page — manage and launch games.
class HomePage extends StatefulWidget {
  const HomePage({super.key});

  @override
  State<HomePage> createState() => _HomePageState();
}

class _HomePageState extends State<HomePage> {
  final GameManager _gameManager = GameManager();
  bool _loading = true;
  String? _iosGamesDir;
  // On Android/iOS the engine is always built-in; EngineMode switching is
  // only meaningful on desktop platforms.
  EngineMode _engineMode = EngineMode.builtIn;
  String? _customDylibPath;
  String? _builtInDylibPath;
  bool _builtInAvailable = false;
  bool _perfOverlay = false;
  bool _fpsLimitEnabled = false;
  int _targetFps = PrefsKeys.defaultFps;
  String _renderer = PrefsKeys.rendererOpengl;
  String _angleBackend = PrefsKeys.angleBackendGles;
  bool _forceLandscape = true;
  bool _pluginTrace = false;
  bool _mockEnabled = true;
  bool _consoleLogFile = true;

  String? _resolveBuiltInDylibPath() {
    if (Platform.isIOS) {
      return '__static_linked__';
    }
    if (Platform.isAndroid) {
      // On Android, native libs are bundled in the APK and loaded
      // automatically via DynamicLibrary.open('libengine_api.so').
      return '__bundled_in_apk__';
    }
    try {
      final executablePath = Platform.resolvedExecutable;
      final execDir = File(executablePath).parent.path;
      final frameworksPath = '$execDir/../Frameworks/libengine_api.dylib';
      final resolved = File(frameworksPath);
      if (resolved.existsSync()) return resolved.path;
    } catch (_) {}
    return null;
  }

  String? get _effectiveDylibPath {
    if (_engineMode == EngineMode.builtIn) {
      // On iOS and Android, the engine is loaded automatically by the system;
      // no explicit dylib path is needed.
      if (Platform.isIOS || Platform.isAndroid) return null;
      return _builtInDylibPath;
    }
    return _customDylibPath;
  }

  @override
  void initState() {
    super.initState();
    _loadGames();
  }

  Future<void> _loadGames() async {
    final prefs = await SharedPreferences.getInstance();
    _builtInDylibPath = _resolveBuiltInDylibPath();
    _builtInAvailable = _builtInDylibPath != null;
    if (Platform.isAndroid || Platform.isIOS) {
      // Mobile platforms always use the bundled engine; skip mode loading.
      _engineMode = EngineMode.builtIn;
      _customDylibPath = null;
    } else {
      final modeStr = prefs.getString(PrefsKeys.engineMode);
      _engineMode = modeStr == PrefsKeys.engineModeCustom ? EngineMode.custom : EngineMode.builtIn;
      _customDylibPath = prefs.getString(PrefsKeys.dylibPath);
    }
    _perfOverlay = prefs.getBool(PrefsKeys.perfOverlay) ?? false;
    _fpsLimitEnabled = prefs.getBool(PrefsKeys.fpsLimitEnabled) ?? false;
    _targetFps = prefs.getInt(PrefsKeys.targetFps) ?? PrefsKeys.defaultFps;
    if (!PrefsKeys.fpsOptions.contains(_targetFps)) _targetFps = PrefsKeys.defaultFps;
    _renderer = prefs.getString(PrefsKeys.renderer) ?? PrefsKeys.rendererOpengl;
    _angleBackend = prefs.getString(PrefsKeys.angleBackend) ?? PrefsKeys.angleBackendGles;
    _forceLandscape = prefs.getBool(PrefsKeys.forceLandscape) ?? true;
    _pluginTrace = prefs.getBool(PrefsKeys.pluginTrace) ?? false;
    _mockEnabled = prefs.getBool(PrefsKeys.mockEnabled) ?? true;
    _consoleLogFile = prefs.getBool(PrefsKeys.consoleLogFile) ?? true;
    await _gameManager.load();
    await _gameManager.applyPendingPlaySession();

    if (Platform.isIOS) {
      await _initIosGamesDir();
    }

    if (mounted) setState(() => _loading = false);
  }

  Future<void> _initIosGamesDir() async {
    final docDir = await getApplicationDocumentsDirectory();
    final gamesDir = Directory('${docDir.path}/Games');
    if (!await gamesDir.exists()) {
      await gamesDir.create(recursive: true);
    }
    _iosGamesDir = gamesDir.path;
    await _scanIosGamesDir();
  }

  Future<void> _scanIosGamesDir() async {
    if (_iosGamesDir == null) return;
    final gamesDir = Directory(_iosGamesDir!);
    if (!await gamesDir.exists()) return;

    // Remove stale entries whose directories no longer exist on disk
    // (handles iOS sandbox UUID changes leaving orphaned entries).
    final stale = <String>[];
    for (final g in _gameManager.games) {
      if (!await Directory(g.path).exists()) {
        stale.add(g.path);
      }
    }
    for (final path in stale) {
      await _gameManager.removeGame(path);
    }

    final existingByName = <String, GameInfo>{};
    for (final g in _gameManager.games) {
      existingByName[p.basename(g.path)] = g;
    }

    final entries = await gamesDir.list().toList();
    final scannedNames = <String>{};
    for (final entry in entries) {
      if (entry is Directory) {
        final name = p.basename(entry.path);
        scannedNames.add(name);
        final existing = existingByName[name];
        if (existing != null) {
          if (existing.path != entry.path) {
            await _gameManager.updateGamePath(existing.path, entry.path);
          }
        } else {
          final game = GameInfo(path: entry.path);
          await _gameManager.addGame(game);
        }
      }
    }

    final toRemove = _gameManager.games
        .where((g) =>
            g.path.startsWith(_iosGamesDir!) &&
            !scannedNames.contains(p.basename(g.path)))
        .map((g) => g.path)
        .toList();
    for (final path in toRemove) {
      await _gameManager.removeGame(path);
    }
  }

  Future<void> _addGame() async {
    final l10n = AppLocalizations.of(context)!;
    if (Platform.isIOS) {
      await _scanIosGamesDir();
      if (mounted) {
        setState(() {});
        _showIosImportGuide();
      }
      return;
    }

    const sheetRadius = BorderRadius.vertical(top: Radius.circular(16));
    final source = await showModalBottomSheet<String>(
      context: context,
      backgroundColor: Colors.transparent,
      builder: (ctx) => Material(
        color: Theme.of(ctx).colorScheme.surface,
        borderRadius: sheetRadius,
        clipBehavior: Clip.antiAlias,
        child: SafeArea(
          child: Padding(
            padding: const EdgeInsets.symmetric(vertical: 8),
            child: Column(
              mainAxisSize: MainAxisSize.min,
              children: [
                ListTile(
                  leading: const Icon(Icons.folder_open),
                  title: Text(l10n.selectGameDirectory),
                  onTap: () => Navigator.pop(ctx, 'directory'),
                ),
                ListTile(
                  leading: const Icon(Icons.archive),
                  title: Text(l10n.selectGameArchive),
                  onTap: () => Navigator.pop(ctx, 'xp3'),
                ),
              ],
            ),
          ),
        ),
      ),
    );
    if (source == null || !mounted) return;

    if (source == 'directory') {
      await _addGameDirectory();
    } else {
      await _addGameArchive();
    }
  }

  Future<void> _addGameDirectory() async {
    final l10n = AppLocalizations.of(context)!;
    final String? selectedDirectory =
        await FilePicker.platform.getDirectoryPath(
      dialogTitle: l10n.selectGameDirectory,
    );
    if (selectedDirectory == null || !mounted) return;

    final game = GameInfo(path: selectedDirectory);
    final added = await _gameManager.addGame(game);
    if (mounted) {
      if (added) {
        setState(() {});
        _offerScrapeAfterAdd(selectedDirectory);
      } else {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(
            content: Text(l10n.gameAlreadyExists(game.displayTitle)),
            behavior: SnackBarBehavior.floating,
          ),
        );
      }
    }
  }

  static const MethodChannel _platformChannel = MethodChannel(
    'flutter_engine_bridge',
  );

  Future<void> _addGameArchive() async {
    final l10n = AppLocalizations.of(context)!;

    if (Platform.isAndroid) {
      // Native picker: zero-copy, returns real filesystem path directly.
      final realPath = await _platformChannel.invokeMethod<String>('pickFile');
      if (realPath == null || !mounted) return;
      if (!realPath.toLowerCase().endsWith('.xp3')) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(
            content: Text(l10n.selectGameArchive),
            behavior: SnackBarBehavior.floating,
          ),
        );
        return;
      }
      final game = GameInfo(path: realPath);
      final added = await _gameManager.addGame(game);
      if (mounted) {
        if (added) {
          setState(() {});
          _offerScrapeAfterAdd(realPath);
        } else {
          ScaffoldMessenger.of(context).showSnackBar(
            SnackBar(
              content: Text(l10n.gameAlreadyExists(p.basename(realPath))),
              behavior: SnackBarBehavior.floating,
            ),
          );
        }
      }
      return;
    }

    final result = await FilePicker.platform.pickFiles(
      dialogTitle: l10n.selectGameArchive,
      type: FileType.custom,
      allowedExtensions: ['xp3', 'XP3'],
      allowMultiple: true,
    );
    if (result == null || result.files.isEmpty || !mounted) return;

    final xp3Files = result.files.where((f) {
      final path = f.path;
      if (path == null) return false;
      return path.toLowerCase().endsWith('.xp3');
    }).toList();

    if (xp3Files.isEmpty) {
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(
            content: Text(l10n.selectGameArchive),
            behavior: SnackBarBehavior.floating,
          ),
        );
      }
      return;
    }

    int addedCount = 0;
    String? lastAddedPath;
    for (final file in xp3Files) {
      final filePath = file.path;
      if (filePath == null) continue;
      final game = GameInfo(path: filePath);
      if (await _gameManager.addGame(game)) {
        addedCount++;
        lastAddedPath = filePath;
      }
    }
    if (mounted) {
      if (addedCount > 0) {
        setState(() {});
        if (addedCount == 1 && lastAddedPath != null) {
          _offerScrapeAfterAdd(lastAddedPath!);
        }
      } else {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(
            content: Text(l10n.gameAlreadyExists(xp3Files.first.name)),
            behavior: SnackBarBehavior.floating,
          ),
        );
      }
    }
  }

  void _showIosImportGuide() {
    final l10n = AppLocalizations.of(context)!;
    showDialog<void>(
      context: context,
      builder: (ctx) => AlertDialog(
        title: Text(l10n.importGames),
        content: Column(
          mainAxisSize: MainAxisSize.min,
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text(l10n.importGamesDesc, style: const TextStyle(fontSize: 14)),
            const SizedBox(height: 16),
            Container(
              padding: const EdgeInsets.all(12),
              decoration: BoxDecoration(
                color: Theme.of(ctx).colorScheme.surfaceContainerHighest,
                borderRadius: BorderRadius.circular(8),
              ),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text(l10n.importStep1, style: const TextStyle(fontSize: 13)),
                  const SizedBox(height: 6),
                  Text(l10n.importStep2,
                      style: const TextStyle(
                          fontSize: 13, fontWeight: FontWeight.w600)),
                  const SizedBox(height: 6),
                  Text(l10n.importStep3,
                      style: const TextStyle(fontSize: 13)),
                  const SizedBox(height: 6),
                  Text(l10n.importStep4,
                      style: const TextStyle(fontSize: 13)),
                ],
              ),
            ),
            const SizedBox(height: 12),
            Text(
              l10n.gamesDirectory,
              style: TextStyle(
                fontSize: 12,
                fontFamily: 'monospace',
                color: Theme.of(ctx)
                    .colorScheme
                    .onSurface
                    .withValues(alpha: 0.5),
              ),
            ),
          ],
        ),
        actions: [
          FilledButton(
            onPressed: () => Navigator.pop(ctx),
            child: Text(l10n.gotIt),
          ),
        ],
      ),
    );
  }

  Future<void> _removeGame(GameInfo game) async {
    final l10n = AppLocalizations.of(context)!;
    final confirmed = await showDialog<bool>(
      context: context,
      builder: (ctx) => AlertDialog(
        title: Text(l10n.removeGame),
        content: Text(l10n.removeGameConfirm(game.displayTitle)),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(ctx, false),
            child: Text(l10n.cancel),
          ),
          FilledButton(
            onPressed: () => Navigator.pop(ctx, true),
            child: Text(l10n.remove),
          ),
        ],
      ),
    );
    if (confirmed == true && mounted) {
      await _gameManager.removeGame(game.path);
      setState(() {});
    }
  }

  Future<void> _setCoverImage(GameInfo game) async {
    final l10n = AppLocalizations.of(context)!;
    final source = await showModalBottomSheet<String>(
      context: context,
      builder: (ctx) => SafeArea(
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            ListTile(
              leading: const Icon(Icons.photo_library),
              title: Text(l10n.coverFromGallery),
              onTap: () => Navigator.pop(ctx, 'gallery'),
            ),
            ListTile(
              leading: const Icon(Icons.camera_alt),
              title: Text(l10n.coverFromCamera),
              onTap: () => Navigator.pop(ctx, 'camera'),
            ),
            if (game.coverPath != null)
              ListTile(
                leading: const Icon(Icons.delete_outline, color: AppColors.errorCrimson),
                title: Text(l10n.coverRemove, style: const TextStyle(color: AppColors.errorCrimson)),
                onTap: () => Navigator.pop(ctx, 'remove'),
              ),
          ],
        ),
      ),
    );
    if (source == null || !mounted) return;

    if (source == 'remove') {
      await _gameManager.setCoverImage(game.path, null);
      if (mounted) setState(() {});
      return;
    }

    final picker = ImagePicker();
    final XFile? image = await picker.pickImage(
      source: source == 'camera' ? ImageSource.camera : ImageSource.gallery,
      maxWidth: 512,
      maxHeight: 512,
      imageQuality: 85,
    );
    if (image == null || !mounted) return;

    // Copy image to app's persistent storage
    final docDir = await getApplicationDocumentsDirectory();
    final coversDir = Directory('${docDir.path}/covers');
    if (!await coversDir.exists()) {
      await coversDir.create(recursive: true);
    }
    final ext = image.path.split('.').last;
    final fileName = '${game.path.hashCode}_${DateTime.now().millisecondsSinceEpoch}.$ext';
    final destPath = '${coversDir.path}/$fileName';
    await File(image.path).copy(destPath);

    // Remove old cover file if exists
    if (game.coverPath != null) {
      try {
        final oldFile = File(game.coverPath!);
        if (await oldFile.exists()) await oldFile.delete();
      } catch (_) {}
    }

    await _gameManager.setCoverImage(game.path, destPath);
    if (mounted) setState(() {});
  }

  Future<void> _renameGame(GameInfo game) async {
    final l10n = AppLocalizations.of(context)!;
    final controller = TextEditingController(text: game.displayTitle);
    final newName = await showDialog<String>(
      context: context,
      builder: (ctx) => AlertDialog(
        title: Text(l10n.renameGame),
        content: TextField(
          controller: controller,
          autofocus: true,
          decoration: InputDecoration(
            border: const OutlineInputBorder(),
            labelText: l10n.displayTitle,
          ),
          onSubmitted: (value) => Navigator.pop(ctx, value),
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(ctx),
            child: Text(l10n.cancel),
          ),
          FilledButton(
            onPressed: () => Navigator.pop(ctx, controller.text),
            child: Text(l10n.save),
          ),
        ],
      ),
    );
    controller.dispose();
    if (newName != null && newName.isNotEmpty && mounted) {
      await _gameManager.renameGame(game.path, newName);
      setState(() {});
    }
  }

  void _launchGame(GameInfo game) {
    final l10n = AppLocalizations.of(context)!;
    final dylibPath = _effectiveDylibPath;
    final isSystemLoadedBuiltIn =
        (Platform.isIOS || Platform.isAndroid) && _engineMode == EngineMode.builtIn;
    if (dylibPath == null && !isSystemLoadedBuiltIn) {
      final msg = _engineMode == EngineMode.builtIn
          ? l10n.engineNotFoundBuiltIn
          : l10n.engineNotFoundCustom;
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(
          content: Text(msg),
          behavior: SnackBarBehavior.floating,
          action: SnackBarAction(
            label: l10n.settings,
            onPressed: _openSettings,
          ),
        ),
      );
      return;
    }
    _gameManager.markPlayed(game.path);
    Navigator.of(context).push(
      AppAnimations.fadeRoute(
        GamePage(
          gamePath: game.path,
          ffiLibraryPath: dylibPath,
          forceLandscape: _forceLandscape,
          pluginTrace: _pluginTrace,
          mockEnabled: _mockEnabled,
          consoleLogFile: _consoleLogFile,
          gameManager: _gameManager,
        ),
      ),
    );
  }

  Future<void> _openGameDetail(GameInfo game) async {
    final result = await Navigator.of(context).push<GameDetailResult>(
      AppAnimations.spatialRoute<GameDetailResult>(
        GameDetailPage(
          game: game,
          gameManager: _gameManager,
        ),
      ),
    );
    if (result == null || !mounted) return;
    if (result.needsRefresh) setState(() {});
    if (result.shouldLaunch) _launchGame(game);
  }

  /// After adding a game, offer to scrape. If user chooses Yes, open detail page with scrape dialog.
  Future<void> _offerScrapeAfterAdd(String addedPath) async {
    final l10n = AppLocalizations.of(context)!;
    final idx = _gameManager.games.indexWhere((g) => g.path == addedPath);
    if (idx < 0 || !mounted) return;
    final game = _gameManager.games[idx];
    final shouldScrape = await showDialog<bool>(
      context: context,
      builder: (ctx) => AlertDialog(
        title: Text(l10n.scrapeMetadata),
        content: Text(l10n.scrapeAfterAddPrompt),
        actions: [
          TextButton(
            onPressed: () => Navigator.of(ctx).pop(false),
            child: Text(l10n.scrapeAfterAddNo),
          ),
          FilledButton(
            onPressed: () => Navigator.of(ctx).pop(true),
            child: Text(l10n.scrapeAfterAddYes),
          ),
        ],
      ),
    );
    if (shouldScrape != true || !mounted) return;
    final result = await Navigator.of(context).push<GameDetailResult>(
      AppAnimations.spatialRoute<GameDetailResult>(
        GameDetailPage(
          game: game,
          gameManager: _gameManager,
          openScrapeOnLoad: true,
        ),
      ),
    );
    if (result == null || !mounted) return;
    if (result.needsRefresh) setState(() {});
    if (result.shouldLaunch) _launchGame(game);
  }

  Future<void> _packUnpackGame(GameInfo game) async {
    final l10n = AppLocalizations.of(context)!;
    final isXp3 = game.path.toLowerCase().endsWith('.xp3');

    final progress = ValueNotifier<double>(0.0);
    final currentFile = ValueNotifier<String>('');

    showDialog<void>(
      context: context,
      barrierDismissible: false,
      builder: (ctx) => PopScope(
        canPop: false,
        child: AlertDialog(
          title: Text(isXp3 ? l10n.unpackingProgress : l10n.packingProgress),
          content: Column(
            mainAxisSize: MainAxisSize.min,
            children: [
              ValueListenableBuilder<double>(
                valueListenable: progress,
                builder: (_, value, _) =>
                    LinearProgressIndicator(value: value),
              ),
              const SizedBox(height: 12),
              ValueListenableBuilder<String>(
                valueListenable: currentFile,
                builder: (_, value, _) => Text(
                  value,
                  style: Theme.of(ctx).textTheme.bodySmall,
                  maxLines: 1,
                  overflow: TextOverflow.ellipsis,
                ),
              ),
            ],
          ),
        ),
      ),
    );

    try {
      if (isXp3) {
        final destDir = p.join(
          p.dirname(game.path),
          p.basenameWithoutExtension(game.path),
        );
        await xp3Extract(
          game.path,
          destDir,
          onProgress: (p, f) {
            progress.value = p;
            currentFile.value = f;
          },
        );
        if (mounted) {
          Navigator.of(context).pop();
          final newGame = GameInfo(path: destDir);
          final added = await _gameManager.addGame(newGame);
          if (added && mounted) setState(() {});
          if (mounted) {
            ScaffoldMessenger.of(context).showSnackBar(
              SnackBar(
                content: Text(l10n.unpackComplete),
                behavior: SnackBarBehavior.floating,
              ),
            );
            if (added) _offerScrapeAfterAdd(destDir);
          }
        }
      } else {
        final xp3Path = '${game.path}.xp3';
        await xp3Pack(
          game.path,
          xp3Path,
          onProgress: (p, f) {
            progress.value = p;
            currentFile.value = f;
          },
        );
        if (mounted) {
          Navigator.of(context).pop();
          final newGame = GameInfo(path: xp3Path);
          final added = await _gameManager.addGame(newGame);
          if (added && mounted) setState(() {});
          if (mounted) {
            ScaffoldMessenger.of(context).showSnackBar(
              SnackBar(
                content: Text(l10n.packComplete),
                behavior: SnackBarBehavior.floating,
              ),
            );
            if (added) _offerScrapeAfterAdd(xp3Path);
          }
        }
      }
    } catch (e) {
      if (mounted) {
        Navigator.of(context).pop();
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(
            content: Text(l10n.xp3OperationFailed(e.toString())),
            behavior: SnackBarBehavior.floating,
          ),
        );
      }
    } finally {
      progress.dispose();
      currentFile.dispose();
    }
  }

  Future<void> _openSettings() async {
    final result = await Navigator.of(context).push<SettingsResult>(
      AppAnimations.fadeRoute<SettingsResult>(
        SettingsPage(
          engineMode: _engineMode,
          customDylibPath: _customDylibPath,
          builtInDylibPath: _builtInDylibPath,
          builtInAvailable: _builtInAvailable,
          perfOverlay: _perfOverlay,
          fpsLimitEnabled: _fpsLimitEnabled,
          targetFps: _targetFps,
          renderer: _renderer,
          angleBackend: _angleBackend,
          forceLandscape: _forceLandscape,
          pluginTrace: _pluginTrace,
          mockEnabled: _mockEnabled,
          consoleLogFile: _consoleLogFile,
        ),
      ),
    );
    if (result != null && mounted) {
      setState(() {
        // On mobile the engine mode is always builtIn, so we skip updating it.
        if (!Platform.isAndroid && !Platform.isIOS) {
          _engineMode = result.engineMode;
          _customDylibPath = result.customDylibPath;
        }
        _perfOverlay = result.perfOverlay;
        _fpsLimitEnabled = result.fpsLimitEnabled;
        _targetFps = result.targetFps;
        _renderer = result.renderer;
        _angleBackend = result.angleBackend;
        _forceLandscape = result.forceLandscape;
        _pluginTrace = result.pluginTrace;
        _mockEnabled = result.mockEnabled;
        _consoleLogFile = result.consoleLogFile;
      });
    }
  }

  int _gridCrossAxisCount(double width) {
    if (width >= 1200) return 5;
    if (width >= 800) return 4;
    if (width >= 400) return 3;
    return 2;
  }

  List<GameInfo> get _sortedGames {
    final sorted = List<GameInfo>.from(_gameManager.games)
      ..sort((a, b) {
        if (a.lastPlayed != null && b.lastPlayed != null) {
          return b.lastPlayed!.compareTo(a.lastPlayed!);
        }
        if (a.lastPlayed != null) return -1;
        if (b.lastPlayed != null) return 1;
        return a.displayTitle.compareTo(b.displayTitle);
      });
    return sorted;
  }

  @override
  Widget build(BuildContext context) {
    final l10n = AppLocalizations.of(context)!;
    final colorScheme = Theme.of(context).colorScheme;
    final games = _sortedGames;
    final isDesktop = !Platform.isAndroid && !Platform.isIOS;
    final topPadding = MediaQuery.of(context).padding.top;

    return Scaffold(
      extendBodyBehindAppBar: true,
      body: _loading
          ? Center(
              child: TweenAnimationBuilder<double>(
                tween: Tween(begin: 0.0, end: 1.0),
                duration: AppAnimations.gentle,
                curve: AppAnimations.warmEaseOut,
                builder: (_, value, __) => Opacity(
                  opacity: value,
                  child: const CircularProgressIndicator(),
                ),
              ),
            )
          : CustomScrollView(
              slivers: [
                SliverPadding(
                  padding: EdgeInsets.only(
                    top: topPadding + 24,
                    left: 24,
                    right: 24,
                    bottom: 8,
                  ),
                  sliver: SliverToBoxAdapter(
                    child: Row(
                      crossAxisAlignment: CrossAxisAlignment.center,
                      children: [
                        Expanded(
                          child: Text(
                            l10n.appTitle,
                            style: Theme.of(context).textTheme.headlineMedium,
                          ),
                        ),
                        if (isDesktop)
                          Tooltip(
                            message: _engineMode == EngineMode.builtIn
                                ? (_builtInAvailable
                                    ? l10n.builtInReady
                                    : l10n.builtInNotReady)
                                : (_customDylibPath != null
                                    ? _customDylibPath!.split('/').last
                                    : l10n.customNotSet),
                            child: Icon(
                              _engineMode == EngineMode.builtIn
                                  ? Icons.inventory_2
                                  : Icons.extension,
                              color: _effectiveDylibPath != null
                                  ? colorScheme.primary
                                  : colorScheme.error,
                              size: 22,
                            ),
                          ),
                        const SizedBox(width: 4),
                        IconButton(
                          icon: const Icon(Icons.settings),
                          tooltip: l10n.settings,
                          onPressed: _openSettings,
                        ),
                      ],
                    ),
                  ),
                ),
                if (games.isEmpty)
                  SliverFillRemaining(
                    hasScrollBody: false,
                    child: _buildEmptyState(colorScheme, l10n),
                  )
                else
                  _buildGameGrid(games, l10n),
                const SliverPadding(padding: EdgeInsets.only(bottom: 88)),
              ],
            ),
      floatingActionButton: Platform.isIOS
          ? Row(
              mainAxisSize: MainAxisSize.min,
              children: [
                FloatingActionButton.extended(
                  heroTag: 'refresh',
                  onPressed: () async {
                    await _scanIosGamesDir();
                    if (mounted) setState(() {});
                  },
                  icon: const Icon(Icons.refresh),
                  label: Text(l10n.refresh),
                ),
                const SizedBox(width: 12),
                FloatingActionButton.extended(
                  heroTag: 'guide',
                  onPressed: _showIosImportGuide,
                  icon: const Icon(Icons.help_outline),
                  label: Text(l10n.howToImport),
                ),
              ],
            )
          : FloatingActionButton.extended(
              onPressed: _addGame,
              icon: const Icon(Icons.add),
              label: Text(l10n.addGame),
            ),
    );
  }

  Widget _buildEmptyState(ColorScheme colorScheme, AppLocalizations l10n) {
    return Center(
      child: Padding(
        padding: const EdgeInsets.symmetric(horizontal: 32),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            Icon(
              Icons.videogame_asset_off,
              size: 72,
              color: colorScheme.primary.withValues(alpha: 0.3),
            ),
            const SizedBox(height: 32),
            Text(
              l10n.noGamesYet,
              style: TextStyle(
                fontFamily: 'Georgia',
                fontSize: 22,
                fontWeight: FontWeight.w500,
                height: 1.20,
                color: colorScheme.onSurface,
              ),
            ),
            const SizedBox(height: 12),
            Text(
              Platform.isIOS ? l10n.noGamesHintIos : l10n.noGamesHintDesktop,
              textAlign: TextAlign.center,
              style: TextStyle(
                fontSize: 15,
                height: 1.60,
                color: colorScheme.onSurfaceVariant,
              ),
            ),
          ],
        ),
      ),
    );
  }

  Widget _buildGameGrid(List<GameInfo> games, AppLocalizations l10n) {
    return SliverLayoutBuilder(
      builder: (context, constraints) {
        final crossAxisCount = _gridCrossAxisCount(constraints.crossAxisExtent);
        return SliverPadding(
          padding: const EdgeInsets.symmetric(horizontal: 20),
          sliver: SliverGrid(
            gridDelegate: SliverGridDelegateWithFixedCrossAxisCount(
              crossAxisCount: crossAxisCount,
              mainAxisSpacing: 16,
              crossAxisSpacing: 16,
              childAspectRatio: 3 / 4,
            ),
            delegate: SliverChildBuilderDelegate(
              (context, index) {
                final game = games[index];
                return AppAnimations.staggeredEntrance(
                  index: index,
                  child: _CoverCard(
                    game: game,
                    l10n: l10n,
                    onTap: () => _openGameDetail(game),
                    onRename: () => _renameGame(game),
                    onRemove: () => _removeGame(game),
                    onSetCover: () => _setCoverImage(game),
                    onPackUnpack: () => _packUnpackGame(game),
                  ),
                );
              },
              childCount: games.length,
            ),
          ),
        );
      },
    );
  }
}

class _CoverCard extends StatefulWidget {
  const _CoverCard({
    required this.game,
    required this.l10n,
    required this.onTap,
    required this.onRename,
    required this.onRemove,
    required this.onSetCover,
    required this.onPackUnpack,
  });

  final GameInfo game;
  final AppLocalizations l10n;
  final VoidCallback onTap;
  final VoidCallback onRename;
  final VoidCallback onRemove;
  final VoidCallback onSetCover;
  final VoidCallback onPackUnpack;

  @override
  State<_CoverCard> createState() => _CoverCardState();
}

class _CoverCardState extends State<_CoverCard> {
  double _scale = 1.0;

  GameInfo get game => widget.game;
  AppLocalizations get l10n => widget.l10n;
  bool get _isXp3 => game.path.toLowerCase().endsWith('.xp3');
  bool get _hasCover =>
      game.coverPath != null && File(game.coverPath!).existsSync();

  void _showContextMenu(BuildContext context) {
    final RenderBox box = context.findRenderObject() as RenderBox;
    final Offset offset = box.localToGlobal(Offset.zero);
    showMenu<String>(
      context: context,
      position: RelativeRect.fromLTRB(
        offset.dx + box.size.width,
        offset.dy,
        offset.dx + box.size.width,
        offset.dy + box.size.height,
      ),
      items: [
        PopupMenuItem(value: 'cover', child: _menuTile(Icons.image, l10n.setCover)),
        PopupMenuItem(value: 'rename', child: _menuTile(Icons.edit, l10n.rename)),
        PopupMenuItem(
          value: 'pack_unpack',
          child: _menuTile(
            _isXp3 ? Icons.unarchive : Icons.archive,
            _isXp3 ? l10n.unpackXp3 : l10n.packXp3,
          ),
        ),
        PopupMenuItem(
          value: 'remove',
          child: _menuTile(Icons.delete, l10n.remove, color: AppColors.errorCrimson),
        ),
      ],
    ).then((value) {
      if (value == null) return;
      switch (value) {
        case 'cover':
          widget.onSetCover();
        case 'rename':
          widget.onRename();
        case 'pack_unpack':
          widget.onPackUnpack();
        case 'remove':
          widget.onRemove();
      }
    });
  }

  static Widget _menuTile(IconData icon, String label, {Color? color}) {
    return Row(
      children: [
        Icon(icon, size: 20, color: color),
        const SizedBox(width: 12),
        Text(label, style: color != null ? TextStyle(color: color) : null),
      ],
    );
  }

  @override
  Widget build(BuildContext context) {
    final colorScheme = Theme.of(context).colorScheme;

    return AnimatedScale(
      scale: _scale,
      duration: AppAnimations.quick,
      curve: AppAnimations.warmEaseOut,
      child: Card(
        clipBehavior: Clip.antiAlias,
        elevation: 0,
        shape: RoundedRectangleBorder(
          borderRadius: BorderRadius.circular(16),
          side: BorderSide(
            color: Theme.of(context).colorScheme.outlineVariant,
            width: 1,
          ),
        ),
        child: Stack(
          fit: StackFit.expand,
          children: [
            _buildBackground(colorScheme),
            _buildGradientOverlay(),
            _buildTitleOverlay(game),
            Material(
              color: Colors.transparent,
              child: InkWell(
                onTap: widget.onTap,
                onTapDown: (_) => setState(() => _scale = AppAnimations.cardPressScale),
                onTapUp: (_) => setState(() => _scale = 1.0),
                onTapCancel: () => setState(() => _scale = 1.0),
                onLongPress: () => _showContextMenu(context),
                onSecondaryTap: () => _showContextMenu(context),
              ),
            ),
          ],
        ),
      ),
    );
  }

  Widget _buildBackground(ColorScheme colorScheme) {
    if (_hasCover) {
      return Hero(
        tag: 'cover_card_${game.path}',
        child: Image.file(
          File(game.coverPath!),
          fit: BoxFit.cover,
          errorBuilder: (_, __, ___) => _buildPlaceholder(colorScheme),
        ),
      );
    }
    return Hero(
      tag: 'cover_card_${game.path}',
      child: _buildPlaceholder(colorScheme),
    );
  }

  Widget _buildPlaceholder(ColorScheme colorScheme) {
    // 中性表面色做底，主题色只用在图标上，避免整块饱和色
    return Container(
      decoration: BoxDecoration(
        gradient: LinearGradient(
          begin: Alignment.topLeft,
          end: Alignment.bottomRight,
          colors: [
            colorScheme.surfaceContainerHigh,
            colorScheme.surfaceContainerHighest,
          ],
        ),
      ),
      child: Center(
        child: Icon(
          Icons.videogame_asset,
          size: 48,
          color: colorScheme.primary.withValues(alpha: 0.6),
        ),
      ),
    );
  }

  Widget _buildGradientOverlay() {
    return const Positioned(
      left: 0,
      right: 0,
      bottom: 0,
      height: 80,
      child: DecoratedBox(
        decoration: BoxDecoration(
          gradient: LinearGradient(
            begin: Alignment.topCenter,
            end: Alignment.bottomCenter,
            colors: [
              Colors.transparent,
              Color(0x8C141413),
            ],
          ),
        ),
      ),
    );
  }

  Widget _buildTitleOverlay(GameInfo game) {
    final lastPlayed = game.lastPlayed;
    final totalSeconds = game.playDurationSeconds ?? 0;
    final hasDuration = totalSeconds >= 60;
    return Positioned(
      left: 14,
      right: 14,
      bottom: 12,
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        mainAxisSize: MainAxisSize.min,
        children: [
          Text(
            game.displayTitle,
            style: const TextStyle(
              color: Colors.white,
              fontFamily: 'Georgia',
              fontSize: 13,
              fontWeight: FontWeight.w500,
              height: 1.25,
            ),
            maxLines: 2,
            overflow: TextOverflow.ellipsis,
          ),
          if (lastPlayed != null || hasDuration) ...[
            const SizedBox(height: 4),
            Text(
              [
                if (lastPlayed != null) _formatDate(lastPlayed),
                if (hasDuration) l10n.playDuration(GameInfo.formatPlayDuration(totalSeconds)),
              ].join(' · '),
              style: TextStyle(
                color: Colors.white.withValues(alpha: 0.65),
                fontSize: 11,
                height: 1.30,
              ),
            ),
          ],
        ],
      ),
    );
  }

  String _formatDate(DateTime date) {
    final now = DateTime.now();
    final diff = now.difference(date);
    if (diff.inMinutes < 1) return l10n.justNow;
    if (diff.inHours < 1) return l10n.minutesAgo(diff.inMinutes);
    if (diff.inDays < 1) return l10n.hoursAgo(diff.inHours);
    if (diff.inDays < 7) return l10n.daysAgo(diff.inDays);
    return '${date.year}-${date.month.toString().padLeft(2, '0')}-${date.day.toString().padLeft(2, '0')}';
  }
}
