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
  String _localeCode = 'system';
  String _themeModeCode = 'dark';
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
        _themeModeCode = prefs.getString(PrefsKeys.themeMode) ?? 'dark';
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
    final mode = code == 'light' ? ThemeMode.light : ThemeMode.dark;
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
              ),
            ),
          ],
        ),
        body: ListView(
          padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
          children: [
            // ── Engine section (desktop only) ──
            // On Android/iOS the engine is always bundled; no switching needed.
            if (!Platform.isAndroid && !Platform.isIOS) ...[
              _SectionHeader(
                icon: Icons.settings_applications,
                label: l10n.settingsEngine,
              ),
              Card(
                child: Padding(
                  padding: const EdgeInsets.all(16),
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Text(l10n.engineMode,
                          style: Theme.of(context).textTheme.titleSmall),
                      const SizedBox(height: 8),
                      SizedBox(
                        width: double.infinity,
                        child: SegmentedButton<EngineMode>(
                          segments: [
                            ButtonSegment<EngineMode>(
                              value: EngineMode.builtIn,
                              label: Text(l10n.builtIn),
                              icon: const Icon(Icons.inventory_2),
                            ),
                            ButtonSegment<EngineMode>(
                              value: EngineMode.custom,
                              label: Text(l10n.custom),
                              icon: const Icon(Icons.folder_open),
                            ),
                          ],
                          selected: {_engineMode},
                          onSelectionChanged: (Set<EngineMode> selected) {
                            setState(() => _engineMode = selected.first);
                            _markDirty();
                          },
                        ),
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
              const SizedBox(height: 16),
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
                    padding: const EdgeInsets.fromLTRB(16, 16, 16, 0),
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
                        SizedBox(
                          width: double.infinity,
                          child: SegmentedButton<String>(
                            showSelectedIcon: false,
                            segments: [
                              ButtonSegment<String>(
                                value: 'opengl',
                                label: SvgPicture.asset(
                                  'assets/icons/opengl.svg',
                                  height: 20,
                                  colorFilter: ColorFilter.mode(
                                    Theme.of(context).colorScheme.onSurface,
                                    BlendMode.srcIn,
                                  ),
                                ),
                              ),
                              ButtonSegment<String>(
                                value: 'software',
                                label: Text(l10n.software),
                                icon: const Icon(Icons.computer),
                              ),
                            ],
                            selected: {_renderer},
                            onSelectionChanged: (Set<String> selected) {
                              setState(() => _renderer = selected.first);
                              _markDirty();
                            },
                          ),
                        ),
                      ],
                    ),
                  ),
                  // ── Graphics Backend (Android only) ──
                  if (Platform.isAndroid) ...[                  
                    const Divider(height: 24),
                    Padding(
                      padding: const EdgeInsets.fromLTRB(16, 0, 16, 0),
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
                          SizedBox(
                            width: double.infinity,
                            child: SegmentedButton<String>(
                              showSelectedIcon: false,
                              segments: [
                                ButtonSegment<String>(
                                  value: 'gles',
                                  label: SvgPicture.asset(
                                    'assets/icons/opengles.svg',
                                    height: 20,
                                    colorFilter: ColorFilter.mode(
                                      Theme.of(context).colorScheme.onSurface,
                                      BlendMode.srcIn,
                                    ),
                                  ),
                                ),
                                ButtonSegment<String>(
                                  value: 'vulkan',
                                  label: SvgPicture.asset(
                                    'assets/icons/vulkan.svg',
                                    height: 20,
                                    colorFilter: ColorFilter.mode(
                                      Theme.of(context).colorScheme.onSurface,
                                      BlendMode.srcIn,
                                    ),
                                  ),
                                ),
                              ],
                              selected: {_angleBackend},
                              onSelectionChanged: (Set<String> selected) {
                                setState(() => _angleBackend = selected.first);
                                _markDirty();
                              },
                            ),
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
            const SizedBox(height: 16),

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
                ],
              ),
            ),
            const SizedBox(height: 16),

            // ── General section ──
            _SectionHeader(
              icon: Icons.language,
              label: l10n.settingsGeneral,
            ),
            Card(
              child: Column(
                children: [
                  ListTile(
                    title: Text(l10n.themeMode),
                    trailing: SegmentedButton<String>(
                      segments: [
                        ButtonSegment<String>(
                          value: 'dark',
                          label: Text(l10n.themeDark),
                          icon: const Icon(Icons.dark_mode, size: 18),
                        ),
                        ButtonSegment<String>(
                          value: 'light',
                          label: Text(l10n.themeLight),
                          icon: const Icon(Icons.light_mode, size: 18),
                        ),
                      ],
                      selected: {_themeModeCode},
                      onSelectionChanged: (Set<String> selected) {
                        _changeThemeMode(selected.first);
                      },
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
            const SizedBox(height: 16),

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
                      style: TextStyle(
                        color: colorScheme.primary,
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
                      style: TextStyle(
                        color: colorScheme.primary,
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
          ],
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
            ? Colors.green.withValues(alpha: 0.1)
            : colorScheme.errorContainer.withValues(alpha: 0.3),
        borderRadius: BorderRadius.circular(8),
        border: Border.all(
          color: widget.builtInAvailable
              ? Colors.green.withValues(alpha: 0.3)
              : colorScheme.error.withValues(alpha: 0.3),
        ),
      ),
      child: Row(
        children: [
          Icon(
            widget.builtInAvailable ? Icons.check_circle : Icons.warning_amber,
            color: widget.builtInAvailable
                ? Colors.green
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
                        ? Colors.green
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
            borderRadius: BorderRadius.circular(8),
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
      padding: const EdgeInsets.only(left: 4, bottom: 8, top: 4),
      child: Row(
        children: [
          Icon(icon, size: 18, color: Theme.of(context).colorScheme.primary),
          const SizedBox(width: 8),
          Text(
            label,
            style: Theme.of(context).textTheme.titleSmall?.copyWith(
                  color: Theme.of(context).colorScheme.primary,
                  fontWeight: FontWeight.w600,
                ),
          ),
        ],
      ),
    );
  }
}
