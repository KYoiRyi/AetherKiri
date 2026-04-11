import 'dart:typed_data';

export 'src/ffi/engine_bindings.dart'
    show
        kEngineInputEventBack,
        kEngineInputEventKeyDown,
        kEngineInputEventKeyUp,
        kEngineInputEventPointerDown,
        kEngineInputEventPointerMove,
        kEngineInputEventPointerScroll,
        kEngineInputEventPointerUp,
        kEngineInputEventTextInput,
        kEnginePixelFormatRgba8888,
        kEnginePixelFormatUnknown,
        kEngineStartupStateFailed,
        kEngineStartupStateIdle,
        kEngineStartupStateRunning,
        kEngineStartupStateSucceeded;
export 'src/ffi/engine_ffi.dart'
    show
        EngineFrameData,
        EngineFrameInfo,
        EngineInputEventData,
        EngineMemoryStatsData;

import 'flutter_engine_bridge_platform_interface.dart';
import 'src/ffi/engine_bindings.dart';
import 'src/ffi/engine_ffi.dart';

class FlutterEngineBridge {
  FlutterEngineBridge({
    FlutterEngineBridgePlatform? platform,
    EngineFfiBridge? ffiBridge,
    String? ffiLibraryPath,
  }) : _platform = platform ?? FlutterEngineBridgePlatform.instance,
       _ffiBridge =
           ffiBridge ?? EngineFfiBridge.tryCreate(libraryPath: ffiLibraryPath),
       _ffiInitializationError = ffiBridge == null
           ? EngineFfiBridge.lastCreateError
           : '';

  final FlutterEngineBridgePlatform _platform;
  final EngineFfiBridge? _ffiBridge;
  final String _ffiInitializationError;
  String _fallbackLastError = '';

  bool get isFfiAvailable => _ffiBridge != null;
  String get ffiInitializationError => _ffiInitializationError;

  Future<String?> getPlatformVersion() {
    return _platform.getPlatformVersion();
  }

  Future<String> getBackendDescription() async {
    if (isFfiAvailable) {
      return 'ffi';
    }
    final platformVersion = await _platform.getPlatformVersion();
    final versionLabel = platformVersion ?? 'unknown';
    return 'method_channel($versionLabel)';
  }

  Future<int> engineCreate({String? writablePath, String? cachePath}) async {
    return _withFfiCall(
      apiName: 'engine_create',
      call: (ffi) =>
          ffi.create(writablePath: writablePath, cachePath: cachePath),
    );
  }

  Future<int> engineDestroy() async {
    return _withFfiCall(
      apiName: 'engine_destroy',
      call: (ffi) => ffi.destroy(),
    );
  }

  Future<int> engineOpenGame(
    String gameRootPath, {
    String? startupScript,
  }) async {
    return _withFfiCall(
      apiName: 'engine_open_game',
      call: (ffi) => ffi.openGame(gameRootPath, startupScript: startupScript),
    );
  }

  Future<int> engineOpenGameAsync(
    String gameRootPath, {
    String? startupScript,
  }) async {
    return _withFfiCall(
      apiName: 'engine_open_game_async',
      call: (ffi) =>
          ffi.openGameAsync(gameRootPath, startupScript: startupScript),
    );
  }

  Future<int?> engineGetStartupState() async {
    final ffi = _ffiBridge;
    if (ffi == null) {
      return null;
    }
    final state = ffi.getStartupState();
    if (state == null) {
      _fallbackLastError = ffi.lastError();
    } else {
      _fallbackLastError = '';
    }
    return state;
  }

  Future<String> engineDrainStartupLogs() async {
    final ffi = _ffiBridge;
    if (ffi == null) {
      return '';
    }
    final logs = ffi.drainStartupLogs();
    if (logs.isEmpty) {
      final err = ffi.lastError();
      if (err.isNotEmpty) {
        _fallbackLastError = err;
      }
    } else {
      _fallbackLastError = '';
    }
    return logs;
  }

  Future<int> engineTick({int deltaMs = 16}) async {
    return _withFfiCall(
      apiName: 'engine_tick',
      call: (ffi) => ffi.tick(deltaMs: deltaMs),
    );
  }

  Future<int> enginePause() async {
    return _withFfiCall(apiName: 'engine_pause', call: (ffi) => ffi.pause());
  }

  Future<int> engineResume() async {
    return _withFfiCall(apiName: 'engine_resume', call: (ffi) => ffi.resume());
  }

  Future<int> engineSetOption({
    required String key,
    required String value,
  }) async {
    return _withFfiCall(
      apiName: 'engine_set_option',
      call: (ffi) => ffi.setOption(key: key, value: value),
    );
  }

  Future<int> engineSetSurfaceSize({
    required int width,
    required int height,
  }) async {
    return _withFfiCall(
      apiName: 'engine_set_surface_size',
      call: (ffi) => ffi.setSurfaceSize(width: width, height: height),
    );
  }

  Future<EngineFrameInfo?> engineGetFrameDesc() async {
    final ffi = _ffiBridge;
    if (ffi == null) {
      final platformVersion = await _platform.getPlatformVersion();
      final versionLabel = platformVersion ?? 'unknown';
      _fallbackLastError = _buildFallbackError(
        'FFI unavailable for engine_get_frame_desc. '
        'MethodChannel fallback active ($versionLabel).',
      );
      return null;
    }

    final frame = ffi.getFrameDesc();
    if (frame == null) {
      _fallbackLastError = ffi.lastError();
    } else {
      _fallbackLastError = '';
    }
    return frame;
  }

  Future<Uint8List?> engineReadFrameRgba() async {
    final ffi = _ffiBridge;
    if (ffi == null) {
      final platformVersion = await _platform.getPlatformVersion();
      final versionLabel = platformVersion ?? 'unknown';
      _fallbackLastError = _buildFallbackError(
        'FFI unavailable for engine_read_frame_rgba. '
        'MethodChannel fallback active ($versionLabel).',
      );
      return null;
    }

    final frameData = ffi.readFrameRgba();
    if (frameData == null) {
      _fallbackLastError = ffi.lastError();
    } else {
      _fallbackLastError = '';
    }
    return frameData;
  }

  Future<EngineFrameData?> engineReadFrame() async {
    final ffi = _ffiBridge;
    if (ffi == null) {
      final platformVersion = await _platform.getPlatformVersion();
      final versionLabel = platformVersion ?? 'unknown';
      _fallbackLastError = _buildFallbackError(
        'FFI unavailable for engine_read_frame_rgba. '
        'MethodChannel fallback active ($versionLabel).',
      );
      return null;
    }

    final frameData = ffi.readFrameRgbaWithDesc();
    if (frameData == null) {
      _fallbackLastError = ffi.lastError();
    } else {
      _fallbackLastError = '';
    }
    return frameData;
  }

  Future<int> engineSendInput(EngineInputEventData event) async {
    return _withFfiCall(
      apiName: 'engine_send_input',
      call: (ffi) => ffi.sendInput(event),
    );
  }

  Future<String?> engineGetMainMenuJson() async {
    final ffi = _ffiBridge;
    if (ffi == null) {
      final platformVersion = await _platform.getPlatformVersion();
      final versionLabel = platformVersion ?? 'unknown';
      _fallbackLastError = _buildFallbackError(
        'FFI unavailable for engine_get_main_menu_json. '
        'MethodChannel fallback active ($versionLabel).',
      );
      return null;
    }

    final json = ffi.getMainMenuJson();
    if (json == null) {
      _fallbackLastError = ffi.lastError();
    } else {
      _fallbackLastError = '';
    }
    return json;
  }

  Future<int> engineActivateMenuItem(String path) async {
    return _withFfiCall(
      apiName: 'engine_activate_menu_item',
      call: (ffi) => ffi.activateMenuItem(path),
    );
  }

  Future<int> engineSetRenderTargetIOSurface({
    required int iosurfaceId,
    required int width,
    required int height,
  }) async {
    return _withFfiCall(
      apiName: 'engine_set_render_target_iosurface',
      call: (ffi) => ffi.setRenderTargetIOSurface(
        iosurfaceId: iosurfaceId,
        width: width,
        height: height,
      ),
    );
  }

  Future<bool> engineGetFrameRenderedFlag() async {
    final ffi = _ffiBridge;
    if (ffi == null) {
      return false;
    }
    return ffi.getFrameRenderedFlag();
  }

  Future<int?> createTexture({required int width, required int height}) async {
    try {
      return await _platform.createTexture(width: width, height: height);
    } catch (error) {
      _fallbackLastError = 'createTexture failed: $error';
      return null;
    }
  }

  Future<bool> updateTextureRgba({
    required int textureId,
    required Uint8List rgba,
    required int width,
    required int height,
    required int rowBytes,
  }) async {
    try {
      await _platform.updateTextureRgba(
        textureId: textureId,
        rgba: rgba,
        width: width,
        height: height,
        rowBytes: rowBytes,
      );
      return true;
    } catch (error) {
      _fallbackLastError = 'updateTextureRgba failed: $error';
      return false;
    }
  }

  Future<void> disposeTexture({required int textureId}) async {
    try {
      await _platform.disposeTexture(textureId: textureId);
    } catch (error) {
      _fallbackLastError = 'disposeTexture failed: $error';
    }
  }

  /// Creates an IOSurface-backed texture for zero-copy rendering.
  /// Returns a map with: textureId, ioSurfaceID, width, height.
  Future<Map<String, dynamic>?> createIOSurfaceTexture({
    required int width,
    required int height,
  }) async {
    try {
      return await _platform.createIOSurfaceTexture(
        width: width,
        height: height,
      );
    } catch (error) {
      _fallbackLastError = 'createIOSurfaceTexture failed: $error';
      return null;
    }
  }

  /// Notifies Flutter that a new frame is available in the texture.
  Future<void> notifyFrameAvailable({required int textureId}) async {
    try {
      await _platform.notifyFrameAvailable(textureId: textureId);
    } catch (error) {
      _fallbackLastError = 'notifyFrameAvailable failed: $error';
    }
  }

  /// Resizes an IOSurface texture.
  Future<Map<String, dynamic>?> resizeIOSurfaceTexture({
    required int textureId,
    required int width,
    required int height,
  }) async {
    try {
      return await _platform.resizeIOSurfaceTexture(
        textureId: textureId,
        width: width,
        height: height,
      );
    } catch (error) {
      _fallbackLastError = 'resizeIOSurfaceTexture failed: $error';
      return null;
    }
  }

  /// Disposes an IOSurface texture.
  Future<void> disposeIOSurfaceTexture({required int textureId}) async {
    try {
      await _platform.disposeIOSurfaceTexture(textureId: textureId);
    } catch (error) {
      _fallbackLastError = 'disposeIOSurfaceTexture failed: $error';
    }
  }

  /// Creates a SurfaceTexture-backed texture for zero-copy rendering (Android).
  /// Returns a map with: textureId, width, height.
  Future<Map<String, dynamic>?> createSurfaceTexture({
    required int width,
    required int height,
  }) async {
    try {
      return await _platform.createSurfaceTexture(width: width, height: height);
    } catch (error) {
      _fallbackLastError = 'createSurfaceTexture failed: $error';
      return null;
    }
  }

  /// Resizes a SurfaceTexture.
  Future<Map<String, dynamic>?> resizeSurfaceTexture({
    required int textureId,
    required int width,
    required int height,
  }) async {
    try {
      return await _platform.resizeSurfaceTexture(
        textureId: textureId,
        width: width,
        height: height,
      );
    } catch (error) {
      _fallbackLastError = 'resizeSurfaceTexture failed: $error';
      return null;
    }
  }

  /// Disposes a SurfaceTexture.
  Future<void> disposeSurfaceTexture({required int textureId}) async {
    try {
      await _platform.disposeSurfaceTexture(textureId: textureId);
    } catch (error) {
      _fallbackLastError = 'disposeSurfaceTexture failed: $error';
    }
  }

  Future<int> engineRuntimeApiVersion() async {
    final ffi = _ffiBridge;
    if (ffi == null) {
      final platformVersion = await _platform.getPlatformVersion();
      final versionLabel = platformVersion ?? 'unknown';
      _fallbackLastError = _buildFallbackError(
        'FFI unavailable for engine_get_runtime_api_version. '
        'MethodChannel fallback active ($versionLabel).',
      );
      return kEngineResultNotSupported;
    }

    final resultOrVersion = ffi.runtimeApiVersion();
    if (resultOrVersion < 0) {
      _fallbackLastError = ffi.lastError();
      return resultOrVersion;
    }

    _fallbackLastError = '';
    return resultOrVersion;
  }

  String engineGetRendererInfo() {
    final ffi = _ffiBridge;
    if (ffi == null) {
      return '';
    }
    return ffi.getRendererInfo();
  }

  Future<EngineMemoryStatsData?> engineGetMemoryStats() async {
    final ffi = _ffiBridge;
    if (ffi == null) {
      return null;
    }
    final stats = ffi.getMemoryStats();
    if (stats == null) {
      _fallbackLastError = ffi.lastError();
    } else {
      _fallbackLastError = '';
    }
    return stats;
  }

  String engineGetLastError() {
    final ffi = _ffiBridge;
    if (ffi == null) {
      return _fallbackLastError;
    }
    return ffi.lastError();
  }

  Future<int> _withFfiCall({
    required String apiName,
    required int Function(EngineFfiBridge ffi) call,
    int fallbackResult = kEngineResultNotSupported,
  }) async {
    final ffi = _ffiBridge;
    if (ffi == null) {
      final platformVersion = await _platform.getPlatformVersion();
      final versionLabel = platformVersion ?? 'unknown';
      _fallbackLastError = _buildFallbackError(
        'FFI unavailable for $apiName. MethodChannel fallback active ($versionLabel).',
      );
      return fallbackResult;
    }

    final result = call(ffi);
    if (result != kEngineResultOk) {
      _fallbackLastError = ffi.lastError();
    } else {
      _fallbackLastError = '';
    }
    return result;
  }

  String _buildFallbackError(String base) {
    if (_ffiInitializationError.isEmpty) {
      return base;
    }
    return '$base\nFFI initialization error:\n$_ffiInitializationError';
  }
}
