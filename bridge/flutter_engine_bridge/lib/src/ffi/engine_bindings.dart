import 'dart:ffi';
import 'dart:io';

import 'package:ffi/ffi.dart';

const int kEngineApiVersion = 0x01000000;
const int kEngineResultOk = 0;
const int kEngineResultInvalidArgument = -1;
const int kEngineResultNotSupported = -3;
const int kEngineStartupStateIdle = 0;
const int kEngineStartupStateRunning = 1;
const int kEngineStartupStateSucceeded = 2;
const int kEngineStartupStateFailed = 3;
const int kEnginePixelFormatUnknown = 0;
const int kEnginePixelFormatRgba8888 = 1;

const int kEngineInputEventPointerDown = 1;
const int kEngineInputEventPointerMove = 2;
const int kEngineInputEventPointerUp = 3;
const int kEngineInputEventPointerScroll = 4;
const int kEngineInputEventKeyDown = 5;
const int kEngineInputEventKeyUp = 6;
const int kEngineInputEventTextInput = 7;
const int kEngineInputEventBack = 8;

final class EngineCreateDesc extends Struct {
  @Uint32()
  external int structSize;

  @Uint32()
  external int apiVersion;

  external Pointer<Utf8> writablePathUtf8;
  external Pointer<Utf8> cachePathUtf8;
  external Pointer<Void> userData;

  @Uint64()
  external int reservedU640;

  @Uint64()
  external int reservedU641;

  @Uint64()
  external int reservedU642;

  @Uint64()
  external int reservedU643;

  external Pointer<Void> reservedPtr0;
  external Pointer<Void> reservedPtr1;
  external Pointer<Void> reservedPtr2;
  external Pointer<Void> reservedPtr3;
}

final class EngineOption extends Struct {
  external Pointer<Utf8> keyUtf8;
  external Pointer<Utf8> valueUtf8;

  @Uint64()
  external int reservedU640;

  @Uint64()
  external int reservedU641;

  external Pointer<Void> reservedPtr0;
  external Pointer<Void> reservedPtr1;
}

final class EngineFrameDesc extends Struct {
  @Uint32()
  external int structSize;

  @Uint32()
  external int width;

  @Uint32()
  external int height;

  @Uint32()
  external int strideBytes;

  @Uint32()
  external int pixelFormat;

  @Uint64()
  external int frameSerial;

  @Uint64()
  external int reservedU640;

  @Uint64()
  external int reservedU641;

  @Uint64()
  external int reservedU642;

  @Uint64()
  external int reservedU643;

  external Pointer<Void> reservedPtr0;
  external Pointer<Void> reservedPtr1;
  external Pointer<Void> reservedPtr2;
  external Pointer<Void> reservedPtr3;
}

final class EngineMemoryStats extends Struct {
  @Uint32()
  external int structSize;

  @Uint32()
  external int selfUsedMb;

  @Uint32()
  external int systemFreeMb;

  @Uint32()
  external int systemTotalMb;

  @Uint64()
  external int graphicCacheBytes;

  @Uint64()
  external int graphicCacheLimitBytes;

  @Uint64()
  external int xp3SegmentCacheBytes;

  @Uint64()
  external int psbCacheBytes;

  @Uint32()
  external int psbCacheEntries;

  @Uint32()
  external int psbCacheEntryLimit;

  @Uint64()
  external int psbCacheHits;

  @Uint64()
  external int psbCacheMisses;

  @Uint32()
  external int archiveCacheEntries;

  @Uint32()
  external int archiveCacheLimit;

  @Uint32()
  external int autopathCacheEntries;

  @Uint32()
  external int autopathCacheLimit;

  @Uint32()
  external int autopathTableEntries;

  @Uint32()
  external int reservedU32;

  @Uint64()
  external int reservedU640;

  @Uint64()
  external int reservedU641;

  @Uint64()
  external int reservedU642;

  @Uint64()
  external int reservedU643;

  external Pointer<Void> reservedPtr0;
  external Pointer<Void> reservedPtr1;
  external Pointer<Void> reservedPtr2;
  external Pointer<Void> reservedPtr3;
}

final class EngineInputEvent extends Struct {
  @Uint32()
  external int structSize;

  @Uint32()
  external int type;

  @Uint64()
  external int timestampMicros;

  @Double()
  external double x;

  @Double()
  external double y;

  @Double()
  external double deltaX;

  @Double()
  external double deltaY;

  @Int32()
  external int pointerId;

  @Int32()
  external int button;

  @Int32()
  external int keyCode;

  @Int32()
  external int modifiers;

  @Uint32()
  external int unicodeCodepoint;

  @Uint32()
  external int reservedU32;

  @Uint64()
  external int reservedU640;

  @Uint64()
  external int reservedU641;

  external Pointer<Void> reservedPtr0;
  external Pointer<Void> reservedPtr1;
}

typedef _EngineGetRuntimeApiVersionNative = Int32 Function(Pointer<Uint32>);
typedef _EngineGetRuntimeApiVersionDart = int Function(Pointer<Uint32>);

typedef _EngineCreateNative =
    Int32 Function(Pointer<EngineCreateDesc>, Pointer<Pointer<Void>>);
typedef _EngineCreateDart =
    int Function(Pointer<EngineCreateDesc>, Pointer<Pointer<Void>>);

typedef _EngineDestroyNative = Int32 Function(Pointer<Void>);
typedef _EngineDestroyDart = int Function(Pointer<Void>);

typedef _EngineOpenGameNative =
    Int32 Function(Pointer<Void>, Pointer<Utf8>, Pointer<Utf8>);
typedef _EngineOpenGameDart =
    int Function(Pointer<Void>, Pointer<Utf8>, Pointer<Utf8>);
typedef _EngineOpenGameAsyncNative =
    Int32 Function(Pointer<Void>, Pointer<Utf8>, Pointer<Utf8>);
typedef _EngineOpenGameAsyncDart =
    int Function(Pointer<Void>, Pointer<Utf8>, Pointer<Utf8>);
typedef _EngineGetStartupStateNative =
    Int32 Function(Pointer<Void>, Pointer<Uint32>);
typedef _EngineGetStartupStateDart =
    int Function(Pointer<Void>, Pointer<Uint32>);
typedef _EngineDrainStartupLogsNative =
    Int32 Function(Pointer<Void>, Pointer<Utf8>, Uint32, Pointer<Uint32>);
typedef _EngineDrainStartupLogsDart =
    int Function(Pointer<Void>, Pointer<Utf8>, int, Pointer<Uint32>);

typedef _EngineTickNative = Int32 Function(Pointer<Void>, Uint32);
typedef _EngineTickDart = int Function(Pointer<Void>, int);

typedef _EnginePauseNative = Int32 Function(Pointer<Void>);
typedef _EnginePauseDart = int Function(Pointer<Void>);

typedef _EngineResumeNative = Int32 Function(Pointer<Void>);
typedef _EngineResumeDart = int Function(Pointer<Void>);

typedef _EngineSetOptionNative =
    Int32 Function(Pointer<Void>, Pointer<EngineOption>);
typedef _EngineSetOptionDart =
    int Function(Pointer<Void>, Pointer<EngineOption>);

typedef _EngineSetSurfaceSizeNative =
    Int32 Function(Pointer<Void>, Uint32, Uint32);
typedef _EngineSetSurfaceSizeDart = int Function(Pointer<Void>, int, int);

typedef _EngineGetFrameDescNative =
    Int32 Function(Pointer<Void>, Pointer<EngineFrameDesc>);
typedef _EngineGetFrameDescDart =
    int Function(Pointer<Void>, Pointer<EngineFrameDesc>);

typedef _EngineReadFrameRgbaNative =
    Int32 Function(Pointer<Void>, Pointer<Void>, IntPtr);
typedef _EngineReadFrameRgbaDart =
    int Function(Pointer<Void>, Pointer<Void>, int);

typedef _EngineSendInputNative =
    Int32 Function(Pointer<Void>, Pointer<EngineInputEvent>);
typedef _EngineSendInputDart =
    int Function(Pointer<Void>, Pointer<EngineInputEvent>);

typedef _EngineGetMainMenuJsonNative =
    Int32 Function(Pointer<Void>, Pointer<Utf8>, Uint32, Pointer<Uint32>);
typedef _EngineGetMainMenuJsonDart =
    int Function(Pointer<Void>, Pointer<Utf8>, int, Pointer<Uint32>);

typedef _EngineActivateMenuItemNative =
    Int32 Function(Pointer<Void>, Pointer<Utf8>);
typedef _EngineActivateMenuItemDart =
    int Function(Pointer<Void>, Pointer<Utf8>);

typedef _EngineSetRenderTargetIOSurfaceNative =
    Int32 Function(Pointer<Void>, Uint32, Uint32, Uint32);
typedef _EngineSetRenderTargetIOSurfaceDart =
    int Function(Pointer<Void>, int, int, int);

typedef _EngineSetRenderTargetSurfaceNative =
    Int32 Function(Pointer<Void>, Pointer<Void>, Uint32, Uint32);
typedef _EngineSetRenderTargetSurfaceDart =
    int Function(Pointer<Void>, Pointer<Void>, int, int);

typedef _EngineGetFrameRenderedFlagNative =
    Int32 Function(Pointer<Void>, Pointer<Uint32>);
typedef _EngineGetFrameRenderedFlagDart =
    int Function(Pointer<Void>, Pointer<Uint32>);

typedef _EngineGetRendererInfoNative =
    Int32 Function(Pointer<Void>, Pointer<Utf8>, Uint32);
typedef _EngineGetRendererInfoDart =
    int Function(Pointer<Void>, Pointer<Utf8>, int);
typedef _EngineGetMemoryStatsNative =
    Int32 Function(Pointer<Void>, Pointer<EngineMemoryStats>);
typedef _EngineGetMemoryStatsDart =
    int Function(Pointer<Void>, Pointer<EngineMemoryStats>);

typedef _EngineGetLastErrorNative = Pointer<Utf8> Function(Pointer<Void>);
typedef _EngineGetLastErrorDart = Pointer<Utf8> Function(Pointer<Void>);

class EngineBindings {
  EngineBindings(DynamicLibrary library)
    : engineGetRuntimeApiVersion = library
          .lookupFunction<
            _EngineGetRuntimeApiVersionNative,
            _EngineGetRuntimeApiVersionDart
          >('engine_get_runtime_api_version'),
      engineCreate = library
          .lookupFunction<_EngineCreateNative, _EngineCreateDart>(
            'engine_create',
          ),
      engineDestroy = library
          .lookupFunction<_EngineDestroyNative, _EngineDestroyDart>(
            'engine_destroy',
          ),
      engineOpenGame = library
          .lookupFunction<_EngineOpenGameNative, _EngineOpenGameDart>(
            'engine_open_game',
          ),
      engineOpenGameAsync = library
          .lookupFunction<_EngineOpenGameAsyncNative, _EngineOpenGameAsyncDart>(
            'engine_open_game_async',
          ),
      engineGetStartupState = library
          .lookupFunction<
            _EngineGetStartupStateNative,
            _EngineGetStartupStateDart
          >('engine_get_startup_state'),
      engineDrainStartupLogs = library
          .lookupFunction<
            _EngineDrainStartupLogsNative,
            _EngineDrainStartupLogsDart
          >('engine_drain_startup_logs'),
      engineTick = library.lookupFunction<_EngineTickNative, _EngineTickDart>(
        'engine_tick',
      ),
      enginePause = library
          .lookupFunction<_EnginePauseNative, _EnginePauseDart>('engine_pause'),
      engineResume = library
          .lookupFunction<_EngineResumeNative, _EngineResumeDart>(
            'engine_resume',
          ),
      engineSetOption = library
          .lookupFunction<_EngineSetOptionNative, _EngineSetOptionDart>(
            'engine_set_option',
          ),
      engineSetSurfaceSize = library
          .lookupFunction<
            _EngineSetSurfaceSizeNative,
            _EngineSetSurfaceSizeDart
          >('engine_set_surface_size'),
      engineGetFrameDesc = library
          .lookupFunction<_EngineGetFrameDescNative, _EngineGetFrameDescDart>(
            'engine_get_frame_desc',
          ),
      engineReadFrameRgba = library
          .lookupFunction<_EngineReadFrameRgbaNative, _EngineReadFrameRgbaDart>(
            'engine_read_frame_rgba',
          ),
      engineSendInput = library
          .lookupFunction<_EngineSendInputNative, _EngineSendInputDart>(
            'engine_send_input',
          ),
      engineGetMainMenuJson = library
          .lookupFunction<
            _EngineGetMainMenuJsonNative,
            _EngineGetMainMenuJsonDart
          >('engine_get_main_menu_json'),
      engineActivateMenuItem = library
          .lookupFunction<
            _EngineActivateMenuItemNative,
            _EngineActivateMenuItemDart
          >('engine_activate_menu_item'),
      engineSetRenderTargetIOSurface = library
          .lookupFunction<
            _EngineSetRenderTargetIOSurfaceNative,
            _EngineSetRenderTargetIOSurfaceDart
          >('engine_set_render_target_iosurface'),
      engineSetRenderTargetSurface = library
          .lookupFunction<
            _EngineSetRenderTargetSurfaceNative,
            _EngineSetRenderTargetSurfaceDart
          >('engine_set_render_target_surface'),
      engineGetFrameRenderedFlag = library
          .lookupFunction<
            _EngineGetFrameRenderedFlagNative,
            _EngineGetFrameRenderedFlagDart
          >('engine_get_frame_rendered_flag'),
      engineGetRendererInfo = library
          .lookupFunction<
            _EngineGetRendererInfoNative,
            _EngineGetRendererInfoDart
          >('engine_get_renderer_info'),
      engineGetMemoryStats = library
          .lookupFunction<
            _EngineGetMemoryStatsNative,
            _EngineGetMemoryStatsDart
          >('engine_get_memory_stats'),
      engineGetLastError = library
          .lookupFunction<_EngineGetLastErrorNative, _EngineGetLastErrorDart>(
            'engine_get_last_error',
          );

  final int Function(Pointer<Uint32>) engineGetRuntimeApiVersion;
  final int Function(Pointer<EngineCreateDesc>, Pointer<Pointer<Void>>)
  engineCreate;
  final int Function(Pointer<Void>) engineDestroy;
  final int Function(Pointer<Void>, Pointer<Utf8>, Pointer<Utf8>)
  engineOpenGame;
  final int Function(Pointer<Void>, Pointer<Utf8>, Pointer<Utf8>)
  engineOpenGameAsync;
  final int Function(Pointer<Void>, Pointer<Uint32>) engineGetStartupState;
  final int Function(Pointer<Void>, Pointer<Utf8>, int, Pointer<Uint32>)
  engineDrainStartupLogs;
  final int Function(Pointer<Void>, int) engineTick;
  final int Function(Pointer<Void>) enginePause;
  final int Function(Pointer<Void>) engineResume;
  final int Function(Pointer<Void>, Pointer<EngineOption>) engineSetOption;
  final int Function(Pointer<Void>, int, int) engineSetSurfaceSize;
  final int Function(Pointer<Void>, Pointer<EngineFrameDesc>)
  engineGetFrameDesc;
  final int Function(Pointer<Void>, Pointer<Void>, int) engineReadFrameRgba;
  final int Function(Pointer<Void>, Pointer<EngineInputEvent>) engineSendInput;
  final int Function(Pointer<Void>, Pointer<Utf8>, int, Pointer<Uint32>)
  engineGetMainMenuJson;
  final int Function(Pointer<Void>, Pointer<Utf8>) engineActivateMenuItem;
  final int Function(Pointer<Void>, int, int, int)
  engineSetRenderTargetIOSurface;
  final int Function(Pointer<Void>, Pointer<Void>, int, int)
  engineSetRenderTargetSurface;
  final int Function(Pointer<Void>, Pointer<Uint32>) engineGetFrameRenderedFlag;
  final int Function(Pointer<Void>, Pointer<Utf8>, int) engineGetRendererInfo;
  final int Function(Pointer<Void>, Pointer<EngineMemoryStats>)
  engineGetMemoryStats;
  final Pointer<Utf8> Function(Pointer<Void>) engineGetLastError;

  static String _lastLoadError = '';
  static String get lastLoadError => _lastLoadError;

  static DynamicLibrary? tryLoadLibrary({String? overridePath}) {
    final List<String> errors = <String>[];
    _lastLoadError = '';

    final String normalizedOverridePath = overridePath?.trim() ?? '';
    if (normalizedOverridePath.isNotEmpty) {
      try {
        return DynamicLibrary.open(normalizedOverridePath);
      } catch (error) {
        _lastLoadError = 'open("$normalizedOverridePath") failed: $error';
        return null;
      }
    }

    final candidates = _candidateLibraryNames();
    for (final candidate in candidates) {
      try {
        return DynamicLibrary.open(candidate);
      } catch (error) {
        errors.add('open("$candidate") failed: $error');
      }
    }

    try {
      return DynamicLibrary.process();
    } catch (error) {
      errors.add('DynamicLibrary.process() failed: $error');
    }

    try {
      return DynamicLibrary.executable();
    } catch (error) {
      errors.add('DynamicLibrary.executable() failed: $error');
    }

    if (errors.isEmpty) {
      _lastLoadError =
          'No dynamic library candidates available for this platform.';
    } else {
      _lastLoadError = errors.join('\n');
    }

    return null;
  }

  static List<String> _candidateLibraryNames() {
    if (Platform.isWindows) {
      return const ['engine_api.dll', 'libengine_api.dll'];
    }
    if (Platform.isMacOS) {
      return const ['libengine_api.dylib'];
    }
    if (Platform.isAndroid || Platform.isLinux) {
      return const ['libengine_api.so'];
    }
    if (Platform.isIOS) {
      return const [];
    }
    return const [];
  }
}
