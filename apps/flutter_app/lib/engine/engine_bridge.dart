import 'dart:typed_data';

class EngineFrameInfo {
  const EngineFrameInfo({
    required this.width,
    required this.height,
    required this.strideBytes,
    required this.pixelFormat,
    required this.frameSerial,
  });

  final int width;
  final int height;
  final int strideBytes;
  final int pixelFormat;
  final int frameSerial;
}

class EngineFrameData {
  const EngineFrameData({required this.info, required this.pixels});

  final EngineFrameInfo info;
  final Uint8List pixels;
}

class EngineInputEventData {
  const EngineInputEventData({
    required this.type,
    this.timestampMicros = 0,
    this.x = 0,
    this.y = 0,
    this.deltaX = 0,
    this.deltaY = 0,
    this.pointerId = 0,
    this.button = 0,
    this.keyCode = 0,
    this.modifiers = 0,
    this.unicodeCodepoint = 0,
  });

  final int type;
  final int timestampMicros;
  final double x;
  final double y;
  final double deltaX;
  final double deltaY;
  final int pointerId;
  final int button;
  final int keyCode;
  final int modifiers;
  final int unicodeCodepoint;
}

class EngineMemoryStats {
  const EngineMemoryStats({
    required this.selfUsedMb,
    required this.systemFreeMb,
    required this.systemTotalMb,
    required this.graphicCacheBytes,
    required this.graphicCacheLimitBytes,
    required this.xp3SegmentCacheBytes,
    required this.psbCacheBytes,
    required this.psbCacheEntries,
    required this.psbCacheEntryLimit,
    required this.psbCacheHits,
    required this.psbCacheMisses,
    required this.archiveCacheEntries,
    required this.archiveCacheLimit,
    required this.autopathCacheEntries,
    required this.autopathCacheLimit,
    required this.autopathTableEntries,
  });

  final int selfUsedMb;
  final int systemFreeMb;
  final int systemTotalMb;
  final int graphicCacheBytes;
  final int graphicCacheLimitBytes;
  final int xp3SegmentCacheBytes;
  final int psbCacheBytes;
  final int psbCacheEntries;
  final int psbCacheEntryLimit;
  final int psbCacheHits;
  final int psbCacheMisses;
  final int archiveCacheEntries;
  final int archiveCacheLimit;
  final int autopathCacheEntries;
  final int autopathCacheLimit;
  final int autopathTableEntries;
}

enum EngineStartupState { idle, running, succeeded, failed }

abstract interface class EngineBridge {
  bool get isFfiAvailable;
  String get ffiInitializationError;

  Future<String?> getPlatformVersion();
  Future<String> getBackendDescription();

  Future<int> engineCreate({String? writablePath, String? cachePath});
  Future<int> engineDestroy();
  Future<int> engineOpenGame(String gameRootPath, {String? startupScript});
  Future<int> engineOpenGameAsync(String gameRootPath, {String? startupScript});
  Future<EngineStartupState?> engineGetStartupState();
  Future<String> engineDrainStartupLogs();
  Future<int> engineTick({int deltaMs = 16});
  Future<int> enginePause();
  Future<int> engineResume();
  Future<int> engineSetOption({required String key, required String value});
  Future<int> engineSetSurfaceSize({required int width, required int height});
  Future<EngineFrameInfo?> engineGetFrameDesc();
  Future<Uint8List?> engineReadFrameRgba();
  Future<EngineFrameData?> engineReadFrame();
  Future<int> engineSendInput(EngineInputEventData event);
  Future<String?> engineGetMainMenuJson();
  Future<int> engineActivateMenuItem(String path);
  Future<int?> createTexture({required int width, required int height});
  Future<bool> updateTextureRgba({
    required int textureId,
    required Uint8List rgba,
    required int width,
    required int height,
    required int rowBytes,
  });
  Future<void> disposeTexture({required int textureId});
  Future<int> engineRuntimeApiVersion();

  // IOSurface zero-copy rendering
  Future<int> engineSetRenderTargetIOSurface({
    required int iosurfaceId,
    required int width,
    required int height,
  });
  Future<bool> engineGetFrameRenderedFlag();
  Future<Map<String, dynamic>?> createIOSurfaceTexture({
    required int width,
    required int height,
  });
  Future<void> notifyFrameAvailable({required int textureId});
  Future<Map<String, dynamic>?> resizeIOSurfaceTexture({
    required int textureId,
    required int width,
    required int height,
  });
  Future<void> disposeIOSurfaceTexture({required int textureId});

  // SurfaceTexture zero-copy rendering (Android)
  Future<Map<String, dynamic>?> createSurfaceTexture({
    required int width,
    required int height,
  });
  Future<Map<String, dynamic>?> resizeSurfaceTexture({
    required int textureId,
    required int width,
    required int height,
  });
  Future<void> disposeSurfaceTexture({required int textureId});

  String engineGetRendererInfo();
  Future<EngineMemoryStats?> engineGetMemoryStats();
  String engineGetLastError();
}

typedef EngineBridgeBuilder = EngineBridge Function({String? ffiLibraryPath});
