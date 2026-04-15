import 'dart:io';

import 'package:file_picker/file_picker.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_svg/flutter_svg.dart';
import 'package:shared_preferences/shared_preferences.dart';
import 'package:url_launcher/url_launcher.dart';

import '../l10n/app_localizations.dart';
import '../main.dart';
import '../constants/prefs_keys.dart';
import '../theme/app_theme.dart';
import '../theme/app_animations.dart';
import '../widgets/app_sliding_segmented_control.dart';
import 'home_page.dart';

/// Standalone settings page with MD3 styling and i18n support.
class SettingsPage extends StatefulWidget {
  const SettingsPage({
    super.key,
    required this.engineMode,
    required this.customDylibPath,
    required this.builtInDylibPath,
    required this.builtInAvailable,
    required this.perfOverlay,
    required this.fpsLimitEnabled,
    required this.targetFps,
    required this.renderer,
    required this.angleBackend,
    required this.forceLandscape,
    required this.pluginTrace,
    required this.mockEnabled,
    required this.consoleLogFile,
    required this.traceLog,
    required this.exportScripts,
  });

  final EngineMode engineMode;
  final String? customDylibPath;
  final String? builtInDylibPath;
  final bool builtInAvailable;
  final bool perfOverlay;
  final bool fpsLimitEnabled;
  final int targetFps;
  final String renderer;
  final String angleBackend;
  final bool forceLandscape;
  final bool pluginTrace;
  final bool mockEnabled;
  final bool consoleLogFile;
  final bool traceLog;
  final bool exportScripts;

  @override
  State<SettingsPage> createState() => _SettingsPageState();
}

/// Return value from the settings page.
class SettingsResult {
  const SettingsResult({
    required this.engineMode,
    required this.customDylibPath,
    required this.perfOverlay,
    required this.fpsLimitEnabled,
    required this.targetFps,
    required this.renderer,
    required this.angleBackend,
    required this.forceLandscape,
    required this.pluginTrace,
    required this.mockEnabled,
    required this.consoleLogFile,
    required this.traceLog,
    required this.exportScripts,
  });

  final EngineMode engineMode;
  final String? customDylibPath;
  final bool perfOverlay;
  final bool fpsLimitEnabled;
  final int targetFps;
  final String renderer;
  final String angleBackend;
  final bool forceLandscape;
  final bool pluginTrace;
  final bool mockEnabled;
  final bool consoleLogFile;
  final bool traceLog;
  final bool exportScripts;
}

class _SettingsPageState extends State<SettingsPage> {
  late EngineMode _engineMode;
  late String? _customDylibPath;
  late bool _perfOverlay;
  late bool _fpsLimitEnabled;
  late int _targetFps;
  late String _renderer;
  String _angleBackend = PrefsKeys.angleBackendGles;
  late bool _forceLandscape;
  late bool _pluginTrace;
  late bool _mockEnabled;
  late bool _consoleLogFile;
  late bool _traceLog;
  late bool _exportScripts;
  String _localeCode = 'system';
  String _themeModeCode = 'system';
  bool _dirty = false;

  @override
  void initState() {
    super.initState();
    _engineMode = widget.engineMode;
    _customDylibPath = widget.customDylibPath;
    _perfOverlay = widget.perfOverlay;
    _fpsLimitEnabled = widget.fpsLimitEnabled;
    _targetFps = widget.targetFps;
    _renderer = widget.renderer;
    _angleBackend = widget.angleBackend;
    _forceLandscape = widget.forceLandscape;
    _pluginTrace = widget.pluginTrace;
    _mockEnabled = widget.mockEnabled;
    _consoleLogFile = widget.consoleLogFile;
    _traceLog = widget.traceLog;
    _exportScripts = widget.exportScripts;
    _loadLocale();
    _loadThemeMode();
  }

  Future<void> _loadLocale() async {
    final prefs = await SharedPreferences.getInstance();
    if (mounted) {
      setState(() {
        _localeCode = prefs.getString(PrefsKeys.locale) ?? 'system';
      });
    }
  }

  Future<void> _loadThemeMode() async {
    final prefs = await SharedPreferences.getInstance();
    if (mounted) {
      setState(() {
        _themeModeCode = prefs.getString(PrefsKeys.themeMode) ?? 'system';
      });
    }
  }

  Future<void> _save() async {
    final prefs = await SharedPreferences.getInstance();
    await prefs.setString(
      PrefsKeys.engineMode,
      _engineMode == EngineMode.custom ? PrefsKeys.engineModeCustom : PrefsKeys.engineModeBuiltIn,
    );
    if (_customDylibPath != null) {
      await prefs.setString(PrefsKeys.dylibPath, _customDylibPath!);
    } else {
      await prefs.remove(PrefsKeys.dylibPath);
    }
    await prefs.setBool(PrefsKeys.perfOverlay, _perfOverlay);
    await prefs.setBool(PrefsKeys.fpsLimitEnabled, _fpsLimitEnabled);
    await prefs.setInt(PrefsKeys.targetFps, _targetFps);
    await prefs.setString(PrefsKeys.renderer, _renderer);
    await prefs.setString(PrefsKeys.angleBackend, _angleBackend);
    await prefs.setBool(PrefsKeys.forceLandscape, _forceLandscape);
    await prefs.setBool(PrefsKeys.pluginTrace, _pluginTrace);
    await prefs.setBool(PrefsKeys.mockEnabled, _mockEnabled);
    await prefs.setBool(PrefsKeys.consoleLogFile, _consoleLogFile);
    await prefs.setBool(PrefsKeys.traceLog, _traceLog);
    await prefs.setBool(PrefsKeys.exportScripts, _exportScripts);

    if (mounted) {
      Navigator.pop(
        context,
        SettingsResult(
          engineMode: _engineMode,
          customDylibPath: _customDylibPath,
          perfOverlay: _perfOverlay,
          fpsLimitEnabled: _fpsLimitEnabled,
          targetFps: _targetFps,
          renderer: _renderer,
          angleBackend: _angleBackend,
          forceLandscape: _forceLandscape,
          pluginTrace: _pluginTrace,
          mockEnabled: _mockEnabled,
          consoleLogFile: _consoleLogFile,
          traceLog: _traceLog,
          exportScripts: _exportScripts,
        ),
      );
    }
  }

  void _markDirty() {
    if (!_dirty) setState(() => _dirty = true);
  }

  Future<void> _changeLocale(String code) async {
    final prefs = await SharedPreferences.getInstance();
    await prefs.setString(PrefsKeys.locale, code);
    if (!mounted) return;
    setState(() => _localeCode = code);

    // Apply locale change in real-time
    if (code == 'system') {
      Krkr2App.setLocale(context, null);
    } else {
      Krkr2App.setLocale(context, Locale(code));
    }
  }

  Future<void> _changeThemeMode(String code) async {
    final prefs = await SharedPreferences.getInstance();
    await prefs.setString(PrefsKeys.themeMode, code);
    if (!mounted) return;
    setState(() => _themeModeCode = code);

    // Apply theme change in real-time
    final mode = code == 'light'
        ? ThemeMode.light
        : code == 'dark'
            ? ThemeMode.dark
            : ThemeMode.system;
    Krkr2App.setThemeMode(context, mode);
  }

  @override
  Widget build(BuildContext context) {
    final l10n = AppLocalizations.of(context)!;
    final colorScheme = Theme.of(context).colorScheme;

    return PopScope(
      canPop: !_dirty,
      onPopInvokedWithResult: (didPop, _) async {
        if (didPop) return;
        final discard = await showDialog<bool>(
          context: context,
          builder: (ctx) => AlertDialog(
            title: Text(l10n.settings),
            content: const Text('Discard unsaved changes?'),
            actions: [
              TextButton(
                onPressed: () => Navigator.pop(ctx, false),
                child: Text(l10n.cancel),
              ),
              FilledButton(
                onPressed: () => Navigator.pop(ctx, true),
                child: const Text('Discard'),
              ),
            ],
          ),
        );
        if (discard == true && context.mounted) {
          Navigator.pop(context);
        }
      },
      child: Scaffold(
        appBar: AppBar(
          title: Text(l10n.settings),
          actions: [
            Padding(
              padding: const EdgeInsets.only(right: 8),
              child: FilledButton.icon(
                onPressed: _dirty ? _save : null,
                icon: const Icon(Icons.save, size: 18),
                label: Text(l10n.save),
                style: FilledButton.styleFrom(
                  backgroundColor: AppColors.terracottaBrand,
                  foregroundColor: AppColors.ivory,
                  shape: RoundedRectangleBorder(
                    borderRadius: BorderRadius.circular(12),
                  ),
                ),
              ),
            ),
          ],
        ),
        body: ListView(
          padding: const EdgeInsets.symmetric(horizontal: 20, vertical: 12),
          children: <Widget>[
            // ── Engine section (desktop only) ──
            // On Android/iOS the engine is always bundled; no switching needed.
            if (!Platform.isAndroid && !Platform.isIOS) ...[
              _SectionHeader(
                icon: Icons.settings_applications,
                label: l10n.settingsEngine,
              ),
              Card(
                child: Padding(
                  padding: const EdgeInsets.all(20),
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Text(l10n.engineMode,
                          style: Theme.of(context).textTheme.titleSmall),
                      const SizedBox(height: 8),
                      AppSlidingSegmentedControl<EngineMode>(
                        value: _engineMode,
                        segments: {
                          EngineMode.builtIn: Row(
                            mainAxisSize: MainAxisSize.min,
                            children: [
                              const Icon(Icons.inventory_2),
                              const SizedBox(width: 8),
                              Flexible(child: Text(l10n.builtIn, overflow: TextOverflow.ellipsis)),
                            ],
                          ),
                          EngineMode.custom: Row(
                            mainAxisSize: MainAxisSize.min,
                            children: [
                              const Icon(Icons.folder_open),
                              const SizedBox(width: 8),
                              Flexible(child: Text(l10n.custom, overflow: TextOverflow.ellipsis)),
                            ],
                          ),
                        },
                        onChanged: (val) {
                          setState(() => _engineMode = val);
                          _markDirty();
                        },
                      ),
                      const SizedBox(height: 12),
                      if (_engineMode == EngineMode.builtIn)
                        _buildBuiltInStatus(context, l10n, colorScheme),
                      if (_engineMode == EngineMode.custom)
                        _buildCustomDylibPicker(context, l10n, colorScheme),
                    ],
                  ),
                ),
              ),
              const SizedBox(height: 24),
            ],

            // ── Rendering section ──
            _SectionHeader(
              icon: Icons.brush,
              label: l10n.settingsRendering,
            ),
            Card(
              child: Column(
                children: [
                  Padding(
                    padding: const EdgeInsets.fromLTRB(20, 20, 20, 0),
                    child: Column(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                        Text(l10n.renderPipeline,
                            style: Theme.of(context).textTheme.titleSmall),
                        const SizedBox(height: 4),
                        Text(
                          l10n.renderPipelineHint,
                          style: Theme.of(context).textTheme.bodySmall?.copyWith(
                                color: colorScheme.onSurface
                                    .withValues(alpha: 0.6),
                              ),
                        ),
                        const SizedBox(height: 8),
                        AppSlidingSegmentedControl<String>(
                          value: _renderer,
                          segments: {
                            'opengl': SvgPicture.asset(
                              'assets/icons/opengl.svg',
                              height: 20,
                              colorFilter: ColorFilter.mode(
                                _renderer == 'opengl'
                                    ? colorScheme.onPrimaryContainer
                                    : colorScheme.onSurfaceVariant,
                                BlendMode.srcIn,
                              ),
                            ),
                            'software': Row(
                              mainAxisSize: MainAxisSize.min,
                              children: [
                                const Icon(Icons.computer),
                                const SizedBox(width: 8),
                                Flexible(child: Text(l10n.software, overflow: TextOverflow.ellipsis)),
                              ],
                            ),
                          },
                          onChanged: (val) {
                            setState(() => _renderer = val);
                            _markDirty();
                          },
                        ),
                      ],
                    ),
                  ),
                  // ── Graphics Backend (Android only) ──
                  if (Platform.isAndroid) ...[                  
                    const Divider(height: 24),
                    Padding(
                      padding: const EdgeInsets.fromLTRB(20, 0, 20, 0),
                      child: Column(
                        crossAxisAlignment: CrossAxisAlignment.start,
                        children: [
                          Text(l10n.graphicsBackend,
                              style: Theme.of(context).textTheme.titleSmall),
                          const SizedBox(height: 4),
                          Text(
                            l10n.graphicsBackendHint,
                            style: Theme.of(context).textTheme.bodySmall?.copyWith(
                                  color: colorScheme.onSurface
                                      .withValues(alpha: 0.6),
                                ),
                          ),
                          const SizedBox(height: 8),
                          AppSlidingSegmentedControl<String>(
                            value: _angleBackend,
                            segments: {
                              'gles': SvgPicture.asset(
                                'assets/icons/opengles.svg',
                                height: 20,
                                colorFilter: ColorFilter.mode(
                                  _angleBackend == 'gles'
                                      ? colorScheme.onPrimaryContainer
                                      : colorScheme.onSurfaceVariant,
                                  BlendMode.srcIn,
                                ),
                              ),
                              'vulkan': SvgPicture.asset(
                                'assets/icons/vulkan.svg',
                                height: 20,
                                colorFilter: ColorFilter.mode(
                                  _angleBackend == 'vulkan'
                                      ? colorScheme.onPrimaryContainer
                                      : colorScheme.onSurfaceVariant,
                                  BlendMode.srcIn,
                                ),
                              ),
                            },
                            onChanged: (val) {
                              setState(() => _angleBackend = val);
                              _markDirty();
                            },
                          ),
                        ],
                      ),
                    ),
                  ],
                  const Divider(height: 24),
                  SwitchListTile(
                    title: Text(l10n.performanceOverlay),
                    subtitle: Text(l10n.performanceOverlayDesc),
                    value: _perfOverlay,
                    onChanged: (value) {
                      setState(() => _perfOverlay = value);
                      _markDirty();
                    },
                  ),
                  const Divider(height: 1),
                  SwitchListTile(
                    title: Text(l10n.fpsLimitEnabled),
                    subtitle: Text(
                      _fpsLimitEnabled
                          ? l10n.fpsLimitEnabledDesc
                          : l10n.fpsLimitOff,
                    ),
                    value: _fpsLimitEnabled,
                    onChanged: (value) {
                      setState(() => _fpsLimitEnabled = value);
                      _markDirty();
                    },
                  ),
                  if (_fpsLimitEnabled) ...[
                    const Divider(height: 1),
                    ListTile(
                      title: Text(l10n.targetFrameRate),
                      subtitle: Text(l10n.targetFrameRateDesc),
                      trailing: DropdownButton<int>(
                        value: _targetFps,
                        items: PrefsKeys.fpsOptions
                            .map((fps) => DropdownMenuItem<int>(
                                  value: fps,
                                  child: Text(l10n.fpsLabel(fps)),
                                ))
                            .toList(),
                        onChanged: (value) {
                          if (value != null) {
                            setState(() => _targetFps = value);
                            _markDirty();
                          }
                        },
                      ),
                    ),
                  ],
                  if (Platform.isAndroid || Platform.isIOS) ...[
                    const Divider(height: 1),
                    SwitchListTile(
                      title: Text(l10n.forceLandscape),
                      subtitle: Text(l10n.forceLandscapeDesc),
                      value: _forceLandscape,
                      onChanged: (value) {
                        setState(() => _forceLandscape = value);
                        _markDirty();
                      },
                    ),
                  ],
                ],
              ),
            ),
            const SizedBox(height: 24),

            // ── Development section ──
            _SectionHeader(
              icon: Icons.developer_mode,
              label: l10n.settingsDevelopment,
            ),
            Card(
              child: Column(
                children: [
                  SwitchListTile(
                    title: Text(l10n.pluginTrace),
                    subtitle: Text(l10n.pluginTraceDesc),
                    value: _pluginTrace,
                    onChanged: (value) {
                      setState(() => _pluginTrace = value);
                      _markDirty();
                    },
                  ),
                  const Divider(height: 1),
                  SwitchListTile(
                    title: Text(l10n.mockBypass),
                    subtitle: Text(l10n.mockBypassDesc),
                    value: _mockEnabled,
                    onChanged: (value) {
                      setState(() => _mockEnabled = value);
                      _markDirty();
                    },
                  ),
                  const Divider(height: 1),
                  SwitchListTile(
                    title: Text(l10n.consoleLogFile),
                    subtitle: Text(l10n.consoleLogFileDesc),
                    value: _consoleLogFile,
                    onChanged: (value) {
                      setState(() => _consoleLogFile = value);
                      _markDirty();
                    },
                  ),
                  const Divider(height: 1),
                  SwitchListTile(
                    title: Text(l10n.traceLog),
                    subtitle: Text(l10n.traceLogDesc),
                    value: _traceLog,
                    onChanged: (value) {
                      setState(() => _traceLog = value);
                      _markDirty();
                    },
                  ),
                  const Divider(height: 1),
                  SwitchListTile(
                    title: Text(l10n.exportScripts),
                    subtitle: Text(l10n.exportScriptsDesc),
                    value: _exportScripts,
                    onChanged: (value) {
                      setState(() => _exportScripts = value);
                      _markDirty();
                    },
                  ),
                ],
              ),
            ),
            const SizedBox(height: 24),

            // ── General section ──
            _SectionHeader(
              icon: Icons.language,
              label: l10n.settingsGeneral,
            ),
            Card(
              child: Column(
                children: [
                  Padding(
                    padding: const EdgeInsets.symmetric(horizontal: 20, vertical: 16),
                    child: Column(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                        Text(l10n.themeMode, style: Theme.of(context).textTheme.titleSmall),
                        const SizedBox(height: 12),
                        AppSlidingSegmentedControl<String>(
                          value: _themeModeCode,
                          segments: {
                            'system': Row(
                              mainAxisSize: MainAxisSize.min,
                              children: [
                                const Icon(Icons.auto_awesome, size: 18),
                                const SizedBox(width: 6),
                                Flexible(child: Text(l10n.themeSystem, overflow: TextOverflow.ellipsis)),
                              ],
                            ),
                            'dark': Row(
                              mainAxisSize: MainAxisSize.min,
                              children: [
                                const Icon(Icons.dark_mode, size: 18),
                                const SizedBox(width: 6),
                                Flexible(child: Text(l10n.themeDark, overflow: TextOverflow.ellipsis)),
                              ],
                            ),
                            'light': Row(
                              mainAxisSize: MainAxisSize.min,
                              children: [
                                const Icon(Icons.light_mode, size: 18),
                                const SizedBox(width: 6),
                                Flexible(child: Text(l10n.themeLight, overflow: TextOverflow.ellipsis)),
                              ],
                            ),
                          },
                          onChanged: _changeThemeMode,
                        ),
                      ],
                    ),
                  ),
                  const Divider(height: 1),
                  ListTile(
                    title: Text(l10n.language),
                    trailing: DropdownButton<String>(
                      value: _localeCode,
                      items: [
                        DropdownMenuItem(
                          value: 'system',
                          child: Text(l10n.languageSystem),
                        ),
                        DropdownMenuItem(
                          value: 'en',
                          child: Text(l10n.languageEn),
                        ),
                        DropdownMenuItem(
                          value: 'zh',
                          child: Text(l10n.languageZh),
                        ),
                        DropdownMenuItem(
                          value: 'ja',
                          child: Text(l10n.languageJa),
                        ),
                      ],
                      onChanged: (value) {
                        if (value != null) {
                          _changeLocale(value);
                        }
                      },
                    ),
                  ),
                ],
              ),
            ),
            const SizedBox(height: 24),

            // ── About section ──
            _SectionHeader(
              icon: Icons.info_outline,
              label: l10n.settingsAbout,
            ),
            Card(
              child: Column(
                children: [
                  ListTile(
                    leading: const Icon(Icons.science_outlined),
                    title: Text(l10n.version),
                    subtitle: Text(
                      l10n.aboutVersionDesc,
                      style: TextStyle(
                        color: colorScheme.error,
                        fontSize: 12,
                        fontWeight: FontWeight.w500,
                      ),
                    ),
                  ),
                  const Divider(height: 1),
                  ListTile(
                    leading: const Icon(Icons.person_outline),
                    title: Text(l10n.aboutAuthor),
                    trailing: Text(
                      l10n.aboutAuthorName,
                      style: const TextStyle(
                        color: AppColors.terracottaBrand,
                        fontWeight: FontWeight.w600,
                      ),
                    ),
                  ),
                  const Divider(height: 1),
                  ListTile(
                    leading: const Icon(Icons.email_outlined),
                    title: Text(l10n.aboutEmail),
                    trailing: Text(
                      'wangguanzhiabcd@126.com',
                      style: const TextStyle(
                        color: AppColors.terracottaBrand,
                        fontSize: 13,
                      ),
                    ),
                    onTap: () {
                      Clipboard.setData(
                        const ClipboardData(text: 'wangguanzhiabcd@126.com'),
                      );
                      ScaffoldMessenger.of(context).showSnackBar(
                        SnackBar(
                          content: Text(l10n.aboutEmailCopied),
                          duration: const Duration(seconds: 2),
                        ),
                      );
                    },
                  ),
                  const Divider(height: 1),
                  ListTile(
                    leading: const Icon(Icons.code),
                    title: const Text('GitHub (Original)'),
                    subtitle: const Text(
                      'github.com/reAAAq/KrKr2-Next',
                      style: TextStyle(fontSize: 12),
                    ),
                    trailing: Icon(
                      Icons.open_in_new,
                      size: 18,
                      color: colorScheme.onSurface.withValues(alpha: 0.5),
                    ),
                    onTap: () {
                      launchUrl(
                        Uri.parse('https://github.com/reAAAq/KrKr2-Next'),
                        mode: LaunchMode.externalApplication,
                      );
                    },
                  ),
                  const Divider(height: 1),
                  ListTile(
                    leading: const Icon(Icons.code),
                    title: Text(l10n.aboutGithubFork),
                    subtitle: const Text(
                      'github.com/KYoiRyi/AetherKiri',
                      style: TextStyle(fontSize: 12),
                    ),
                    trailing: Icon(
                      Icons.open_in_new,
                      size: 18,
                      color: colorScheme.onSurface.withValues(alpha: 0.5),
                    ),
                    onTap: () {
                      launchUrl(
                        Uri.parse('https://github.com/KYoiRyi/AetherKiri'),
                        mode: LaunchMode.externalApplication,
                      );
                    },
                  ),
                ],
              ),
            ),
            const SizedBox(height: 32),
          ].asMap().entries.map((e) => AppAnimations.staggeredEntrance(
                index: e.key,
                child: e.value,
              )).toList(),
        ),
      ),
    );
  }

  Widget _buildBuiltInStatus(
      BuildContext context, AppLocalizations l10n, ColorScheme colorScheme) {
    return Container(
      padding: const EdgeInsets.all(12),
      decoration: BoxDecoration(
        color: widget.builtInAvailable
            ? AppColors.warmSand.withValues(alpha: 0.4)
            : colorScheme.errorContainer.withValues(alpha: 0.3),
        borderRadius: BorderRadius.circular(12),
        border: Border.all(
          color: widget.builtInAvailable
              ? AppColors.ringWarm
              : colorScheme.error.withValues(alpha: 0.3),
        ),
      ),
      child: Row(
        children: [
          Icon(
            widget.builtInAvailable ? Icons.check_circle : Icons.warning_amber,
            color: widget.builtInAvailable
                ? AppColors.terracottaBrand
                : colorScheme.error,
            size: 20,
          ),
          const SizedBox(width: 10),
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(
                  widget.builtInAvailable
                      ? l10n.builtInEngineAvailable
                      : l10n.builtInEngineNotFound,
                  style: TextStyle(
                    fontSize: 13,
                    fontWeight: FontWeight.w600,
                    color: widget.builtInAvailable
                        ? AppColors.terracottaBrand
                        : colorScheme.error,
                  ),
                ),
                if (!widget.builtInAvailable)
                  Padding(
                    padding: const EdgeInsets.only(top: 4),
                    child: Text(
                      l10n.builtInEngineHint,
                      style: TextStyle(
                        fontSize: 11,
                        color: colorScheme.onSurface.withValues(alpha: 0.6),
                      ),
                    ),
                  ),
              ],
            ),
          ),
        ],
      ),
    );
  }

  Widget _buildCustomDylibPicker(
      BuildContext context, AppLocalizations l10n, ColorScheme colorScheme) {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Text(l10n.engineDylibPath,
            style: Theme.of(context).textTheme.titleSmall),
        const SizedBox(height: 8),
        Container(
          padding: const EdgeInsets.all(12),
          decoration: BoxDecoration(
            color: colorScheme.surfaceContainerHighest,
            borderRadius: BorderRadius.circular(12),
          ),
          child: Row(
            children: [
              Expanded(
                child: Text(
                  _customDylibPath ?? l10n.notSetRequired,
                  style: TextStyle(
                    fontSize: 13,
                    fontFamily: 'monospace',
                    color: _customDylibPath != null
                        ? null
                        : colorScheme.error.withValues(alpha: 0.7),
                  ),
                  maxLines: 2,
                  overflow: TextOverflow.ellipsis,
                ),
              ),
              if (_customDylibPath != null)
                IconButton(
                  icon: const Icon(Icons.clear, size: 18),
                  tooltip: l10n.clearPath,
                  onPressed: () {
                    setState(() => _customDylibPath = null);
                    _markDirty();
                  },
                ),
            ],
          ),
        ),
        const SizedBox(height: 12),
        SizedBox(
          width: double.infinity,
          child: OutlinedButton.icon(
            onPressed: () async {
              final result = await FilePicker.platform.pickFiles(
                dialogTitle: l10n.selectEngineDylib,
                type: FileType.any,
              );
              if (result != null && result.files.single.path != null) {
                setState(() => _customDylibPath = result.files.single.path);
                _markDirty();
              }
            },
            icon: const Icon(Icons.folder_open),
            label: Text(l10n.browse),
          ),
        ),
      ],
    );
  }
}

/// Section header widget for settings groups.
class _SectionHeader extends StatelessWidget {
  const _SectionHeader({required this.icon, required this.label});

  final IconData icon;
  final String label;

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.only(left: 4, bottom: 10, top: 4),
      child: Row(
        children: [
          Icon(icon, size: 18, color: AppColors.terracottaBrand),
          const SizedBox(width: 8),
          Text(
            label,
            style: const TextStyle(
              fontFamily: 'Georgia',
              fontSize: 14,
              fontWeight: FontWeight.w500,
              height: 1.30,
              color: AppColors.terracottaBrand,
            ),
          ),
        ],
      ),
    );
  }
}
