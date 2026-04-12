import 'dart:convert';
import 'dart:ffi';
import 'dart:typed_data';

import 'package:ffi/ffi.dart';

import 'engine_bindings.dart';

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

class EngineMemoryStatsData {
  const EngineMemoryStatsData({
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

class EngineFfiBridge {
  EngineFfiBridge._(this._bindings);

  final EngineBindings _bindings;
  Pointer<Void> _handle = nullptr;
  static String _lastCreateError = '';
  static String get lastCreateError => _lastCreateError;

  static EngineFfiBridge? tryCreate({String? libraryPath}) {
    _lastCreateError = '';
    final library = EngineBindings.tryLoadLibrary(overridePath: libraryPath);
    if (library == null) {
      _lastCreateError =
          'Failed to load engine_api dynamic library.\n${EngineBindings.lastLoadError}';
      return null;
    }
    try {
      return EngineFfiBridge._(EngineBindings(library));
    } catch (error) {
      _lastCreateError = 'Failed to bind engine_api symbols: $error';
      return null;
    }
  }

  bool get isCreated => _handle != nullptr;

  int runtimeApiVersion() {
    final outVersion = calloc<Uint32>();
    try {
      final result = _bindings.engineGetRuntimeApiVersion(outVersion);
      if (result != kEngineResultOk) {
        return result;
      }
      return outVersion.value;
    } finally {
      calloc.free(outVersion);
    }
  }

  int create({String? writablePath, String? cachePath}) {
    if (_handle != nullptr) {
      return kEngineResultOk;
    }

    final desc = calloc<EngineCreateDesc>();
    final outHandle = calloc<Pointer<Void>>();
    final allocatedStrings = <Pointer<Utf8>>[];
    try {
      final writablePtr = _toNativeUtf8OrNull(writablePath, allocatedStrings);
      final cachePtr = _toNativeUtf8OrNull(cachePath, allocatedStrings);

      desc.ref.structSize = sizeOf<EngineCreateDesc>();
      desc.ref.apiVersion = kEngineApiVersion;
      desc.ref.writablePathUtf8 = writablePtr;
      desc.ref.cachePathUtf8 = cachePtr;
      desc.ref.userData = nullptr;
      desc.ref.reservedU640 = 0;
      desc.ref.reservedU641 = 0;
      desc.ref.reservedU642 = 0;
      desc.ref.reservedU643 = 0;
      desc.ref.reservedPtr0 = nullptr;
      desc.ref.reservedPtr1 = nullptr;
      desc.ref.reservedPtr2 = nullptr;
      desc.ref.reservedPtr3 = nullptr;
      outHandle.value = nullptr;

      final result = _bindings.engineCreate(desc, outHandle);
      if (result == kEngineResultOk) {
        _handle = outHandle.value;
      }
      return result;
    } finally {
      for (final value in allocatedStrings) {
        calloc.free(value);
      }
      calloc.free(outHandle);
      calloc.free(desc);
    }
  }

  int destroy() {
    final current = _handle;
    if (current == nullptr) {
      return kEngineResultOk;
    }

    final result = _bindings.engineDestroy(current);
    if (result == kEngineResultOk) {
      _handle = nullptr;
    }
    return result;
  }

  int openGame(String gameRootPath, {String? startupScript}) {
    final gameRootPtr = gameRootPath.toNativeUtf8();
    final startupScriptPtr = startupScript == null
        ? nullptr.cast<Utf8>()
        : startupScript.toNativeUtf8();
    try {
      return _bindings.engineOpenGame(_handle, gameRootPtr, startupScriptPtr);
    } finally {
      calloc.free(gameRootPtr);
      if (startupScriptPtr != nullptr) {
        calloc.free(startupScriptPtr);
      }
    }
  }

  int openGameAsync(String gameRootPath, {String? startupScript}) {
    final gameRootPtr = gameRootPath.toNativeUtf8();
    final startupScriptPtr = startupScript == null
        ? nullptr.cast<Utf8>()
        : startupScript.toNativeUtf8();
    try {
      return _bindings.engineOpenGameAsync(
        _handle,
        gameRootPtr,
        startupScriptPtr,
      );
    } finally {
      calloc.free(gameRootPtr);
      if (startupScriptPtr != nullptr) {
        calloc.free(startupScriptPtr);
      }
    }
  }

  int? getStartupState() {
    final outState = calloc<Uint32>();
    try {
      final result = _bindings.engineGetStartupState(_handle, outState);
      if (result != kEngineResultOk) {
        return null;
      }
      return outState.value;
    } finally {
      calloc.free(outState);
    }
  }

  String drainStartupLogs() {
    const int bufferSize = 65536;
    final buffer = calloc<Uint8>(bufferSize);
    final outBytesWritten = calloc<Uint32>();
    try {
      final result = _bindings.engineDrainStartupLogs(
        _handle,
        buffer.cast<Utf8>(),
        bufferSize,
        outBytesWritten,
      );
      if (result != kEngineResultOk) {
        return '';
      }
      final length = outBytesWritten.value;
      if (length == 0) {
        return '';
      }
      return utf8.decode(buffer.asTypedList(length), allowMalformed: true);
    } finally {
      calloc.free(outBytesWritten);
      calloc.free(buffer);
    }
  }

  int tick({int deltaMs = 16}) {
    return _bindings.engineTick(_handle, deltaMs);
  }

  int pause() {
    return _bindings.enginePause(_handle);
  }

  int resume() {
    return _bindings.engineResume(_handle);
  }

  int setOption({required String key, required String value}) {
    final option = calloc<EngineOption>();
    final keyPtr = key.toNativeUtf8();
    final valuePtr = value.toNativeUtf8();
    try {
      option.ref.keyUtf8 = keyPtr;
      option.ref.valueUtf8 = valuePtr;
      option.ref.reservedU640 = 0;
      option.ref.reservedU641 = 0;
      option.ref.reservedPtr0 = nullptr;
      option.ref.reservedPtr1 = nullptr;
      return _bindings.engineSetOption(_handle, option);
    } finally {
      calloc.free(valuePtr);
      calloc.free(keyPtr);
      calloc.free(option);
    }
  }

  int setSurfaceSize({required int width, required int height}) {
    return _bindings.engineSetSurfaceSize(_handle, width, height);
  }

  EngineFrameInfo? getFrameDesc() {
    final desc = calloc<EngineFrameDesc>();
    try {
      desc.ref.structSize = sizeOf<EngineFrameDesc>();
      final result = _bindings.engineGetFrameDesc(_handle, desc);
      if (result != kEngineResultOk) {
        return null;
      }
      return EngineFrameInfo(
        width: desc.ref.width,
        height: desc.ref.height,
        strideBytes: desc.ref.strideBytes,
        pixelFormat: desc.ref.pixelFormat,
        frameSerial: desc.ref.frameSerial,
      );
    } finally {
      calloc.free(desc);
    }
  }

  Uint8List? readFrameRgba() {
    final frameDesc = getFrameDesc();
    if (frameDesc == null) {
      return null;
    }

    final bufferSize = frameDesc.strideBytes * frameDesc.height;
    if (bufferSize <= 0) {
      return Uint8List(0);
    }

    final buffer = calloc<Uint8>(bufferSize);
    try {
      final result = _bindings.engineReadFrameRgba(
        _handle,
        buffer.cast<Void>(),
        bufferSize,
      );
      if (result != kEngineResultOk) {
        return null;
      }
      return Uint8List.fromList(buffer.asTypedList(bufferSize));
    } finally {
      calloc.free(buffer);
    }
  }

  EngineFrameData? readFrameRgbaWithDesc({int maxRetries = 1}) {
    for (int attempt = 0; attempt <= maxRetries; attempt++) {
      final EngineFrameInfo? frameDesc = getFrameDesc();
      if (frameDesc == null) {
        return null;
      }

      final int bufferSize = frameDesc.strideBytes * frameDesc.height;
      if (bufferSize <= 0) {
        return EngineFrameData(info: frameDesc, pixels: Uint8List(0));
      }

      final Pointer<Uint8> buffer = calloc<Uint8>(bufferSize);
      try {
        final int result = _bindings.engineReadFrameRgba(
          _handle,
          buffer.cast<Void>(),
          bufferSize,
        );
        if (result == kEngineResultOk) {
          return EngineFrameData(
            info: frameDesc,
            pixels: Uint8List.fromList(buffer.asTypedList(bufferSize)),
          );
        }
        // Surface size can race with frame read; retry once with fresh desc.
        if (result == kEngineResultInvalidArgument && attempt < maxRetries) {
          continue;
        }
        return null;
      } finally {
        calloc.free(buffer);
      }
    }
    return null;
  }

  int sendInput(EngineInputEventData event) {
    final nativeEvent = calloc<EngineInputEvent>();
    try {
      nativeEvent.ref.structSize = sizeOf<EngineInputEvent>();
      nativeEvent.ref.type = event.type;
      nativeEvent.ref.timestampMicros = event.timestampMicros;
      nativeEvent.ref.x = event.x;
      nativeEvent.ref.y = event.y;
      nativeEvent.ref.deltaX = event.deltaX;
      nativeEvent.ref.deltaY = event.deltaY;
      nativeEvent.ref.pointerId = event.pointerId;
      nativeEvent.ref.button = event.button;
      nativeEvent.ref.keyCode = event.keyCode;
      nativeEvent.ref.modifiers = event.modifiers;
      nativeEvent.ref.unicodeCodepoint = event.unicodeCodepoint;
      nativeEvent.ref.reservedU32 = 0;
      nativeEvent.ref.reservedU640 = 0;
      nativeEvent.ref.reservedU641 = 0;
      nativeEvent.ref.reservedPtr0 = nullptr;
      nativeEvent.ref.reservedPtr1 = nullptr;
      return _bindings.engineSendInput(_handle, nativeEvent);
    } finally {
      calloc.free(nativeEvent);
    }
  }

  String? getMainMenuJson() {
    const int bufferSize = 262144;
    final buffer = calloc<Uint8>(bufferSize);
    final outBytesWritten = calloc<Uint32>();
    try {
      final result = _bindings.engineGetMainMenuJson(
        _handle,
        buffer.cast<Utf8>(),
        bufferSize,
        outBytesWritten,
      );
      if (result != kEngineResultOk) {
        return null;
      }
      final length = outBytesWritten.value;
      return utf8.decode(buffer.asTypedList(length), allowMalformed: true);
    } finally {
      calloc.free(outBytesWritten);
      calloc.free(buffer);
    }
  }

  int activateMenuItem(String path) {
    final pathPtr = path.toNativeUtf8();
    try {
      return _bindings.engineActivateMenuItem(_handle, pathPtr);
    } finally {
      calloc.free(pathPtr);
    }
  }

  int setRenderTargetIOSurface({
    required int iosurfaceId,
    required int width,
    required int height,
  }) {
    return _bindings.engineSetRenderTargetIOSurface(
      _handle,
      iosurfaceId,
      width,
      height,
    );
  }

  /// Android: set render target surface.
  /// On Android, the surface is set via JNI (nativeSetSurface) from the
  /// Kotlin plugin. This FFI call is used to explicitly detach (pass nullptr).
  int setRenderTargetSurface({required int width, required int height}) {
    // Pass nullptr to detach the surface render target
    return _bindings.engineSetRenderTargetSurface(
      _handle,
      nullptr,
      width,
      height,
    );
  }

  bool getFrameRenderedFlag() {
    final outFlag = calloc<Uint32>();
    try {
      final result = _bindings.engineGetFrameRenderedFlag(_handle, outFlag);
      if (result != kEngineResultOk) {
        return false;
      }
      return outFlag.value != 0;
    } finally {
      calloc.free(outFlag);
    }
  }

  String getRendererInfo() {
    if (_handle == nullptr) {
      return '';
    }
    const int bufferSize = 512;
    final buffer = calloc<Uint8>(bufferSize);
    try {
      final result = _bindings.engineGetRendererInfo(
        _handle,
        buffer.cast<Utf8>(),
        bufferSize,
      );
      if (result != kEngineResultOk) {
        return '';
      }
      return buffer.cast<Utf8>().toDartString();
    } finally {
      calloc.free(buffer);
    }
  }

  EngineMemoryStatsData? getMemoryStats() {
    final stats = calloc<EngineMemoryStats>();
    try {
      stats.ref.structSize = sizeOf<EngineMemoryStats>();
      final result = _bindings.engineGetMemoryStats(_handle, stats);
      if (result != kEngineResultOk) {
        return null;
      }
      return EngineMemoryStatsData(
        selfUsedMb: stats.ref.selfUsedMb,
        systemFreeMb: stats.ref.systemFreeMb,
        systemTotalMb: stats.ref.systemTotalMb,
        graphicCacheBytes: stats.ref.graphicCacheBytes,
        graphicCacheLimitBytes: stats.ref.graphicCacheLimitBytes,
        xp3SegmentCacheBytes: stats.ref.xp3SegmentCacheBytes,
        psbCacheBytes: stats.ref.psbCacheBytes,
        psbCacheEntries: stats.ref.psbCacheEntries,
        psbCacheEntryLimit: stats.ref.psbCacheEntryLimit,
        psbCacheHits: stats.ref.psbCacheHits,
        psbCacheMisses: stats.ref.psbCacheMisses,
        archiveCacheEntries: stats.ref.archiveCacheEntries,
        archiveCacheLimit: stats.ref.archiveCacheLimit,
        autopathCacheEntries: stats.ref.autopathCacheEntries,
        autopathCacheLimit: stats.ref.autopathCacheLimit,
        autopathTableEntries: stats.ref.autopathTableEntries,
      );
    } finally {
      calloc.free(stats);
    }
  }

  String lastError() {
    final errorPtr = _bindings.engineGetLastError(_handle);
    if (errorPtr == nullptr) {
      return '';
    }
    return errorPtr.toDartString();
  }

  static Pointer<Utf8> _toNativeUtf8OrNull(
    String? value,
    List<Pointer<Utf8>> allocatedStrings,
  ) {
    if (value == null) {
      return nullptr.cast<Utf8>();
    }
    final ptr = value.toNativeUtf8();
    allocatedStrings.add(ptr);
    return ptr;
  }
}
