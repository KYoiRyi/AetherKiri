import 'dart:typed_data';

import 'package:flutter_engine_bridge/flutter_engine_bridge.dart'
    as plugin_bridge;

import 'engine_bridge.dart';

EngineBridge createEngineBridge({String? ffiLibraryPath}) {
  return FlutterEngineBridgeAdapter(ffiLibraryPath: ffiLibraryPath);
}

class FlutterEngineBridgeAdapter implements EngineBridge {
  FlutterEngineBridgeAdapter({String? ffiLibraryPath})
    : _delegate = plugin_bridge.FlutterEngineBridge(
        ffiLibraryPath: ffiLibraryPath,
      );

  final plugin_bridge.FlutterEngineBridge _delegate;

  @override
  bool get isFfiAvailable => _delegate.isFfiAvailable;

  @override
  String get ffiInitializationError => _delegate.ffiInitializationError;

  @override
  Future<String?> getPlatformVersion() => _delegate.getPlatformVersion();

  @override
  Future<String> getBackendDescription() => _delegate.getBackendDescription();

  @override
  Future<int> engineCreate({String? writablePath, String? cachePath}) {
    return _delegate.engineCreate(
      writablePath: writablePath,
      cachePath: cachePath,
    );
  }

  @override
  Future<int> engineDestroy() => _delegate.engineDestroy();

  @override
  Future<int> engineOpenGame(String gameRootPath, {String? startupScript}) {
    return _delegate.engineOpenGame(gameRootPath, startupScript: startupScript);
  }

  @override
  Future<int> engineOpenGameAsync(
    String gameRootPath, {
    String? startupScript,
  }) {
    return _delegate.engineOpenGameAsync(
      gameRootPath,
      startupScript: startupScript,
    );
  }

  @override
  Future<EngineStartupState?> engineGetStartupState() async {
    final int? state = await _delegate.engineGetStartupState();
    switch (state) {
      case plugin_bridge.kEngineStartupStateIdle:
        return EngineStartupState.idle;
      case plugin_bridge.kEngineStartupStateRunning:
        return EngineStartupState.running;
      case plugin_bridge.kEngineStartupStateSucceeded:
        return EngineStartupState.succeeded;
      case plugin_bridge.kEngineStartupStateFailed:
        return EngineStartupState.failed;
      default:
        return null;
    }
  }

  @override
  Future<String> engineDrainStartupLogs() {
    return _delegate.engineDrainStartupLogs();
  }

  @override
  Future<int> engineTick({int deltaMs = 16}) {
    return _delegate.engineTick(deltaMs: deltaMs);
  }

  @override
  Future<int> enginePause() => _delegate.enginePause();

  @override
  Future<int> engineResume() => _delegate.engineResume();

  @override
  Future<int> engineSetOption({required String key, required String value}) {
    return _delegate.engineSetOption(key: key, value: value);
  }

  @override
  Future<int> engineSetSurfaceSize({required int width, required int height}) {
    return _delegate.engineSetSurfaceSize(width: width, height: height);
  }

  @override
  Future<EngineFrameInfo?> engineGetFrameDesc() async {
    final frame = await _delegate.engineGetFrameDesc();
    if (frame == null) {
      return null;
    }
    return EngineFrameInfo(
      width: frame.width,
      height: frame.height,
      strideBytes: frame.strideBytes,
      pixelFormat: frame.pixelFormat,
      frameSerial: frame.frameSerial,
    );
  }

  @override
  Future<Uint8List?> engineReadFrameRgba() {
    return _delegate.engineReadFrameRgba();
  }

  @override
  Future<EngineFrameData?> engineReadFrame() async {
    final plugin_bridge.EngineFrameData? frame = await _delegate
        .engineReadFrame();
    if (frame == null) {
      return null;
    }
    return EngineFrameData(
      info: EngineFrameInfo(
        width: frame.info.width,
        height: frame.info.height,
        strideBytes: frame.info.strideBytes,
        pixelFormat: frame.info.pixelFormat,
        frameSerial: frame.info.frameSerial,
      ),
      pixels: frame.pixels,
    );
  }

  @override
  Future<int> engineSendInput(EngineInputEventData event) {
    return _delegate.engineSendInput(
      plugin_bridge.EngineInputEventData(
        type: event.type,
        timestampMicros: event.timestampMicros,
        x: event.x,
        y: event.y,
        deltaX: event.deltaX,
        deltaY: event.deltaY,
        pointerId: event.pointerId,
        button: event.button,
        keyCode: event.keyCode,
        modifiers: event.modifiers,
        unicodeCodepoint: event.unicodeCodepoint,
      ),
    );
  }

  @override
  Future<String?> engineGetMainMenuJson() {
    return _delegate.engineGetMainMenuJson();
  }

  @override
  Future<int> engineActivateMenuItem(String path) {
    return _delegate.engineActivateMenuItem(path);
  }

  @override
  Future<int?> createTexture({required int width, required int height}) {
    return _delegate.createTexture(width: width, height: height);
  }

  @override
  Future<bool> updateTextureRgba({
    required int textureId,
    required Uint8List rgba,
    required int width,
    required int height,
    required int rowBytes,
  }) {
    return _delegate.updateTextureRgba(
      textureId: textureId,
      rgba: rgba,
      width: width,
      height: height,
      rowBytes: rowBytes,
    );
  }

  @override
  Future<void> disposeTexture({required int textureId}) {
    return _delegate.disposeTexture(textureId: textureId);
  }

  @override
  Future<int> engineRuntimeApiVersion() => _delegate.engineRuntimeApiVersion();

  @override
  Future<int> engineSetRenderTargetIOSurface({
    required int iosurfaceId,
    required int width,
    required int height,
  }) {
    return _delegate.engineSetRenderTargetIOSurface(
      iosurfaceId: iosurfaceId,
      width: width,
      height: height,
    );
  }

  @override
  Future<bool> engineGetFrameRenderedFlag() {
    return _delegate.engineGetFrameRenderedFlag();
  }

  @override
  Future<Map<String, dynamic>?> createIOSurfaceTexture({
    required int width,
    required int height,
  }) {
    return _delegate.createIOSurfaceTexture(width: width, height: height);
  }

  @override
  Future<void> notifyFrameAvailable({required int textureId}) {
    return _delegate.notifyFrameAvailable(textureId: textureId);
  }

  @override
  Future<Map<String, dynamic>?> resizeIOSurfaceTexture({
    required int textureId,
    required int width,
    required int height,
  }) {
    return _delegate.resizeIOSurfaceTexture(
      textureId: textureId,
      width: width,
      height: height,
    );
  }

  @override
  Future<void> disposeIOSurfaceTexture({required int textureId}) {
    return _delegate.disposeIOSurfaceTexture(textureId: textureId);
  }

  @override
  Future<Map<String, dynamic>?> createSurfaceTexture({
    required int width,
    required int height,
  }) {
    return _delegate.createSurfaceTexture(width: width, height: height);
  }

  @override
  Future<Map<String, dynamic>?> resizeSurfaceTexture({
    required int textureId,
    required int width,
    required int height,
  }) {
    return _delegate.resizeSurfaceTexture(
      textureId: textureId,
      width: width,
      height: height,
    );
  }

  @override
  Future<void> disposeSurfaceTexture({required int textureId}) {
    return _delegate.disposeSurfaceTexture(textureId: textureId);
  }

  @override
  String engineGetRendererInfo() => _delegate.engineGetRendererInfo();

  @override
  Future<EngineMemoryStats?> engineGetMemoryStats() async {
    final stats = await _delegate.engineGetMemoryStats();
    if (stats == null) {
      return null;
    }
    return EngineMemoryStats(
      selfUsedMb: stats.selfUsedMb,
      systemFreeMb: stats.systemFreeMb,
      systemTotalMb: stats.systemTotalMb,
      graphicCacheBytes: stats.graphicCacheBytes,
      graphicCacheLimitBytes: stats.graphicCacheLimitBytes,
      xp3SegmentCacheBytes: stats.xp3SegmentCacheBytes,
      psbCacheBytes: stats.psbCacheBytes,
      psbCacheEntries: stats.psbCacheEntries,
      psbCacheEntryLimit: stats.psbCacheEntryLimit,
      psbCacheHits: stats.psbCacheHits,
      psbCacheMisses: stats.psbCacheMisses,
      archiveCacheEntries: stats.archiveCacheEntries,
      archiveCacheLimit: stats.archiveCacheLimit,
      autopathCacheEntries: stats.autopathCacheEntries,
      autopathCacheLimit: stats.autopathCacheLimit,
      autopathTableEntries: stats.autopathTableEntries,
    );
  }

  @override
  String engineGetLastError() => _delegate.engineGetLastError();
}
