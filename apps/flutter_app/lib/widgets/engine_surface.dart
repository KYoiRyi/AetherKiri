import 'dart:async';
import 'dart:io';
import 'dart:ui' as ui;

import 'package:flutter/gestures.dart';
import 'package:flutter/material.dart';
import 'package:flutter/scheduler.dart';
import 'package:flutter/services.dart';

import '../engine/engine_bridge.dart';

class EngineInputEventType {
  static const int pointerDown = 1;
  static const int pointerMove = 2;
  static const int pointerUp = 3;
  static const int pointerScroll = 4;
  static const int keyDown = 5;
  static const int keyUp = 6;
  static const int textInput = 7;
  static const int back = 8;
}

/// Rendering mode for the engine surface.
enum EngineSurfaceMode {
  /// GPU zero-copy mode (macOS: IOSurface, Android: SurfaceTexture).
  gpuZeroCopy,

  /// Flutter Texture with RGBA pixel upload (cross-platform, slower).
  texture,

  /// Pure software decoding via RawImage (slowest, always works).
  software,
}

class EngineSurface extends StatefulWidget {
  const EngineSurface({
    super.key,
    required this.bridge,
    required this.active,
    this.surfaceMode = EngineSurfaceMode.gpuZeroCopy,
    this.externalTickDriven = false,
    this.onLog,
    this.onError,
  });

  final EngineBridge bridge;
  final bool active;
  final EngineSurfaceMode surfaceMode;

  /// When true, the internal frame polling timer is disabled.
  /// The parent widget must call [EngineSurfaceState.pollFrame()] after each
  /// engine tick to eliminate the dual-timer phase mismatch.
  final bool externalTickDriven;
  final ValueChanged<String>? onLog;
  final ValueChanged<String>? onError;

  @override
  EngineSurfaceState createState() => EngineSurfaceState();
}

class EngineSurfaceState extends State<EngineSurface> {
  static const MethodChannel _platformChannel = MethodChannel(
    'flutter_engine_bridge',
  );
  final FocusNode _focusNode = FocusNode(debugLabel: 'engine-surface-focus');
  bool _vsyncScheduled = false;
  bool _frameInFlight = false;
  bool _textureInitInFlight = false;
  ui.Image? _frameImage;
  int _lastFrameSerial = -1;
  int _surfaceWidth = 0;
  int _surfaceHeight = 0;
  int _frameWidth = 0;
  int _frameHeight = 0;
  double _devicePixelRatio = 1.0;

  // Legacy texture mode
  int? _textureId;

  // IOSurface zero-copy mode (macOS)
  int? _ioSurfaceTextureId;
  // ignore: unused_field
  int? _ioSurfaceId;
  bool _ioSurfaceInitInFlight = false;

  // SurfaceTexture zero-copy mode (Android)
  int? _surfaceTextureId;
  bool _surfaceTextureInitInFlight = false;
  Size _lastRequestedLogicalSize = Size.zero;
  double _lastRequestedDpr = 1.0;
  EngineInputEventData? _pendingPointerMoveEvent;
  bool _pointerMoveFlushScheduled = false;

  @override
  void initState() {
    super.initState();
    _reconcilePolling();
  }

  @override
  void didUpdateWidget(covariant EngineSurface oldWidget) {
    super.didUpdateWidget(oldWidget);
    if (oldWidget.bridge != widget.bridge) {
      unawaited(_disposeAllTextures());
    }
    if (oldWidget.surfaceMode != widget.surfaceMode) {
      unawaited(_disposeAllTextures());
    }
    if (oldWidget.active != widget.active ||
        oldWidget.bridge != widget.bridge ||
        oldWidget.surfaceMode != widget.surfaceMode ||
        oldWidget.externalTickDriven != widget.externalTickDriven) {
      _reconcilePolling();
    }
  }

  @override
  void dispose() {
    // _vsyncScheduled will simply be ignored once disposed.
    _vsyncScheduled = false;
    _frameImage?.dispose();
    unawaited(_disposeAllTextures());
    _focusNode.dispose();
    super.dispose();
  }

  Future<void> _disposeAllTextures() async {
    await _disposeTexture();
    await _disposeIOSurfaceTexture();
    await _disposeSurfaceTexture();
  }

  void _reconcilePolling() {
    if (!widget.active || widget.externalTickDriven) {
      _vsyncScheduled = false;
      return;
    }

    _scheduleVsyncPoll();
    unawaited(_pollFrame());
  }

  /// Schedule a single vsync-aligned frame callback.
  /// This replaces Timer.periodic and aligns with Flutter's display refresh.
  void _scheduleVsyncPoll() {
    if (_vsyncScheduled || !widget.active || widget.externalTickDriven) {
      return;
    }
    _vsyncScheduled = true;
    SchedulerBinding.instance.scheduleFrameCallback((_) {
      _vsyncScheduled = false;
      if (!mounted || !widget.active || widget.externalTickDriven) {
        return;
      }
      unawaited(_pollFrame());
      _scheduleVsyncPoll();
    });
  }

  /// Public entry point for the parent tick loop to drive frame polling.
  /// Call this immediately after [EngineBridge.engineTick] completes
  /// to eliminate the dual-timer phase mismatch.
  ///
  /// When [rendered] is provided (non-null), the IOSurface path uses it
  /// directly instead of calling [engineGetFrameRenderedFlag] again.
  /// This avoids the double-read problem where the flag is reset on the
  /// first read and the second read always sees false.
  Future<void> pollFrame({bool? rendered}) =>
      _pollFrame(externalRendered: rendered);

  Future<void> _ensureSurfaceSize(Size size, double devicePixelRatio) async {
    if (!widget.active) {
      return;
    }
    _devicePixelRatio = devicePixelRatio <= 0 ? 1.0 : devicePixelRatio;
    final int width = (size.width * _devicePixelRatio).round().clamp(1, 16384);
    final int height = (size.height * _devicePixelRatio).round().clamp(
      1,
      16384,
    );

    if (width == _surfaceWidth && height == _surfaceHeight) {
      await _ensureRenderTarget();
      return;
    }

    _surfaceWidth = width;
    _surfaceHeight = height;
    final int result = await widget.bridge.engineSetSurfaceSize(
      width: width,
      height: height,
    );
    if (result != 0) {
      _reportError(
        'engine_set_surface_size failed: result=$result, error=${widget.bridge.engineGetLastError()}',
      );
      return;
    }
    widget.onLog?.call(
      'surface resized: ${width}x$height (dpr=${_devicePixelRatio.toStringAsFixed(2)})',
    );
    await _ensureRenderTarget();
  }

  Future<void> _ensureRenderTarget() async {
    switch (widget.surfaceMode) {
      case EngineSurfaceMode.gpuZeroCopy:
        if (Platform.isAndroid) {
          await _ensureSurfaceTexture();
        } else {
          await _ensureIOSurfaceTexture();
        }
        break;
      case EngineSurfaceMode.texture:
        await _ensureTexture();
        break;
      case EngineSurfaceMode.software:
        // No texture needed
        break;
    }
  }

  // --- IOSurface zero-copy mode ---

  Future<void> _ensureIOSurfaceTexture() async {
    if (!widget.active ||
        _ioSurfaceInitInFlight ||
        _surfaceWidth <= 0 ||
        _surfaceHeight <= 0) {
      return;
    }

    final int requestedWidth = _surfaceWidth;
    final int requestedHeight = _surfaceHeight;

    // Check if we need to resize
    if (_ioSurfaceTextureId != null) {
      if (_frameWidth == requestedWidth && _frameHeight == requestedHeight) {
        return; // Already at correct size
      }
      // Resize needed
      _ioSurfaceInitInFlight = true;
      try {
        final result = await widget.bridge.resizeIOSurfaceTexture(
          textureId: _ioSurfaceTextureId!,
          width: requestedWidth,
          height: requestedHeight,
        );
        if (result != null && mounted) {
          final int newIOSurfaceId = result['ioSurfaceID'] as int;
          // Tell the engine about the new IOSurface
          final int setResult = await widget.bridge
              .engineSetRenderTargetIOSurface(
                iosurfaceId: newIOSurfaceId,
                width: requestedWidth,
                height: requestedHeight,
              );
          if (setResult == 0) {
            setState(() {
              _ioSurfaceId = newIOSurfaceId;
              _frameWidth = requestedWidth;
              _frameHeight = requestedHeight;
            });
            widget.onLog?.call(
              'IOSurface resized: ${requestedWidth}x$requestedHeight (iosurface=$newIOSurfaceId)',
            );
          } else {
            _reportError(
              'engine_set_render_target_iosurface failed after resize: $setResult',
            );
          }
        }
      } finally {
        _ioSurfaceInitInFlight = false;
      }
      if (mounted &&
          widget.active &&
          (_frameWidth != _surfaceWidth || _frameHeight != _surfaceHeight)) {
        unawaited(_ensureIOSurfaceTexture());
      }
      return;
    }

    _ioSurfaceInitInFlight = true;
    try {
      final result = await widget.bridge.createIOSurfaceTexture(
        width: _surfaceWidth,
        height: _surfaceHeight,
      );

      if (!mounted) return;
      if (result == null) {
        widget.onLog?.call(
          'IOSurface texture unavailable, falling back to legacy texture mode',
        );
        // Fall back to legacy texture mode
        await _ensureTexture();
        return;
      }

      final int textureId = result['textureId'] as int;
      final int ioSurfaceId = result['ioSurfaceID'] as int;

      // Tell the C++ engine to render to this IOSurface
      final int setResult = await widget.bridge.engineSetRenderTargetIOSurface(
        iosurfaceId: ioSurfaceId,
        width: _surfaceWidth,
        height: _surfaceHeight,
      );

      if (setResult != 0) {
        _reportError(
          'engine_set_render_target_iosurface failed: $setResult, '
          'error=${widget.bridge.engineGetLastError()}',
        );
        // Clean up and fall back
        await widget.bridge.disposeIOSurfaceTexture(textureId: textureId);
        await _ensureTexture();
        return;
      }

      final ui.Image? previousImage = _frameImage;
      setState(() {
        _ioSurfaceTextureId = textureId;
        _ioSurfaceId = ioSurfaceId;
        _textureId = null; // Dispose legacy texture if any
        _frameImage = null;
        _frameWidth = _surfaceWidth;
        _frameHeight = _surfaceHeight;
      });
      previousImage?.dispose();
      widget.onLog?.call(
        'IOSurface zero-copy mode enabled (textureId=$textureId, iosurface=$ioSurfaceId)',
      );
    } finally {
      _ioSurfaceInitInFlight = false;
    }
  }

  Future<void> _disposeIOSurfaceTexture() async {
    final int? textureId = _ioSurfaceTextureId;
    if (textureId == null) return;
    _ioSurfaceTextureId = null;
    _ioSurfaceId = null;
    // Detach from engine
    try {
      await widget.bridge.engineSetRenderTargetIOSurface(
        iosurfaceId: 0,
        width: 0,
        height: 0,
      );
    } catch (_) {}
    await widget.bridge.disposeIOSurfaceTexture(textureId: textureId);
  }

  // --- SurfaceTexture zero-copy mode (Android) ---

  Future<void> _ensureSurfaceTexture() async {
    if (!widget.active ||
        _surfaceTextureInitInFlight ||
        _surfaceWidth <= 0 ||
        _surfaceHeight <= 0) {
      return;
    }

    final int requestedWidth = _surfaceWidth;
    final int requestedHeight = _surfaceHeight;

    // Check if we need to resize
    if (_surfaceTextureId != null) {
      if (_frameWidth == requestedWidth && _frameHeight == requestedHeight) {
        return; // Already at correct size
      }
      // Resize needed
      _surfaceTextureInitInFlight = true;
      try {
        final result = await widget.bridge.resizeSurfaceTexture(
          textureId: _surfaceTextureId!,
          width: requestedWidth,
          height: requestedHeight,
        );
        if (result != null && mounted) {
          setState(() {
            _frameWidth = requestedWidth;
            _frameHeight = requestedHeight;
          });
          widget.onLog?.call(
            'SurfaceTexture resized: ${requestedWidth}x$requestedHeight',
          );
        }
      } finally {
        _surfaceTextureInitInFlight = false;
      }
      if (mounted &&
          widget.active &&
          (_frameWidth != _surfaceWidth || _frameHeight != _surfaceHeight)) {
        unawaited(_ensureSurfaceTexture());
      }
      return;
    }

    _surfaceTextureInitInFlight = true;
    try {
      final result = await widget.bridge.createSurfaceTexture(
        width: _surfaceWidth,
        height: _surfaceHeight,
      );

      if (!mounted) return;
      if (result == null) {
        widget.onLog?.call(
          'SurfaceTexture unavailable, falling back to legacy texture mode',
        );
        await _ensureTexture();
        return;
      }

      final int textureId = result['textureId'] as int;

      // The SurfaceTexture/Surface is already passed to C++ via JNI
      // in the Kotlin plugin's createSurfaceTexture method.
      // The C++ engine_tick() will auto-detect the pending ANativeWindow
      // and attach it as the EGL WindowSurface render target.

      final ui.Image? previousImage = _frameImage;
      setState(() {
        _surfaceTextureId = textureId;
        _textureId = null; // Dispose legacy texture if any
        _frameImage = null;
        _frameWidth = _surfaceWidth;
        _frameHeight = _surfaceHeight;
      });
      previousImage?.dispose();
      widget.onLog?.call(
        'SurfaceTexture zero-copy mode enabled (textureId=$textureId)',
      );
    } finally {
      _surfaceTextureInitInFlight = false;
    }
  }

  Future<void> _disposeSurfaceTexture() async {
    final int? textureId = _surfaceTextureId;
    if (textureId == null) return;
    _surfaceTextureId = null;
    await widget.bridge.disposeSurfaceTexture(textureId: textureId);
  }

  // --- Legacy texture mode ---

  Future<void> _ensureTexture() async {
    if (!widget.active || _textureInitInFlight || _textureId != null) {
      return;
    }

    _textureInitInFlight = true;
    try {
      final int? textureId = await widget.bridge.createTexture(
        width: _surfaceWidth > 0 ? _surfaceWidth : 1,
        height: _surfaceHeight > 0 ? _surfaceHeight : 1,
      );

      if (!mounted) {
        if (textureId != null) {
          await widget.bridge.disposeTexture(textureId: textureId);
        }
        return;
      }
      if (textureId == null) {
        widget.onLog?.call('texture unavailable, fallback to software mode');
        return;
      }

      final ui.Image? previousImage = _frameImage;
      setState(() {
        _textureId = textureId;
        _frameImage = null;
      });
      previousImage?.dispose();
      widget.onLog?.call('texture mode enabled (id=$textureId)');
    } finally {
      _textureInitInFlight = false;
    }
  }

  Future<void> _disposeTexture() async {
    final int? textureId = _textureId;
    if (textureId == null) {
      return;
    }
    _textureId = null;
    await widget.bridge.disposeTexture(textureId: textureId);
  }

  // --- Frame polling ---

  Future<void> _pollFrame({bool? externalRendered}) async {
    if (!widget.active || _frameInFlight) {
      return;
    }

    _frameInFlight = true;
    try {
      // IOSurface/SurfaceTexture zero-copy mode: just notify Flutter, no pixel transfer
      if (_ioSurfaceTextureId != null || _surfaceTextureId != null) {
        // When the caller already checked the flag (external tick-driven
        // mode), use that value directly to avoid the double-read problem.
        // engineGetFrameRenderedFlag resets the flag on read, so a second
        // read would always return false.
        final bool rendered =
            externalRendered ??
            await widget.bridge.engineGetFrameRenderedFlag();
        if (rendered && mounted) {
          final int activeZeroCopyTextureId =
              _ioSurfaceTextureId ?? _surfaceTextureId!;
          await widget.bridge.notifyFrameAvailable(
            textureId: activeZeroCopyTextureId,
          );
        }
        return;
      }

      // Legacy path: read pixels
      final EngineFrameData? frameData = await widget.bridge.engineReadFrame();
      if (frameData == null) {
        return;
      }
      final EngineFrameInfo frameInfo = frameData.info;
      final Uint8List rgbaData = frameData.pixels;

      if (frameInfo.width <= 0 || frameInfo.height <= 0) {
        return;
      }
      if (frameInfo.frameSerial == _lastFrameSerial) {
        return;
      }

      final int expectedMinLength = frameInfo.strideBytes * frameInfo.height;
      if (expectedMinLength <= 0 || rgbaData.length < expectedMinLength) {
        _reportError(
          'engine_read_frame_rgba returned insufficient data: '
          'len=${rgbaData.length}, required=$expectedMinLength',
        );
        return;
      }

      final int? textureId = _textureId;
      if (textureId != null) {
        final bool updated = await widget.bridge.updateTextureRgba(
          textureId: textureId,
          rgba: rgbaData,
          width: frameInfo.width,
          height: frameInfo.height,
          rowBytes: frameInfo.strideBytes,
        );
        if (!updated) {
          _reportError(
            'updateTextureRgba failed, fallback to software mode: '
            '${widget.bridge.engineGetLastError()}',
          );
          await _disposeTexture();
        } else if (mounted) {
          setState(() {
            _frameWidth = frameInfo.width;
            _frameHeight = frameInfo.height;
            _lastFrameSerial = frameInfo.frameSerial;
          });
          return;
        }
      }

      final ui.Image nextImage = await _decodeRgbaFrame(
        rgbaData,
        width: frameInfo.width,
        height: frameInfo.height,
        rowBytes: frameInfo.strideBytes,
      );

      if (!mounted) {
        nextImage.dispose();
        return;
      }

      final ui.Image? previousImage = _frameImage;
      setState(() {
        _frameImage = nextImage;
        _frameWidth = frameInfo.width;
        _frameHeight = frameInfo.height;
        _lastFrameSerial = frameInfo.frameSerial;
      });
      previousImage?.dispose();
    } catch (error) {
      _reportError('surface poll failed: $error');
    } finally {
      _frameInFlight = false;
    }
  }

  Future<ui.Image> _decodeRgbaFrame(
    Uint8List pixels, {
    required int width,
    required int height,
    required int rowBytes,
  }) {
    final Completer<ui.Image> completer = Completer<ui.Image>();
    ui.decodeImageFromPixels(pixels, width, height, ui.PixelFormat.rgba8888, (
      ui.Image image,
    ) {
      completer.complete(image);
    }, rowBytes: rowBytes);
    return completer.future;
  }

  void _reportError(String message) {
    widget.onError?.call(message);
  }

  Widget _buildTextureView(int textureId) {
    final bool zeroCopyActive =
        _ioSurfaceTextureId != null || _surfaceTextureId != null;
    // Use physical pixel dimensions, but convert to logical pixels for layout.
    // The Texture widget renders at full physical resolution regardless of
    // the SizedBox logical size, so this only affects aspect ratio calculation.
    final double dpr = _devicePixelRatio > 0 ? _devicePixelRatio : 1.0;
    final int physW = zeroCopyActive
        ? (_surfaceWidth > 0
              ? _surfaceWidth
              : (_frameWidth > 0 ? _frameWidth : 1))
        : (_frameWidth > 0
              ? _frameWidth
              : (_surfaceWidth > 0 ? _surfaceWidth : 1));
    final int physH = zeroCopyActive
        ? (_surfaceHeight > 0
              ? _surfaceHeight
              : (_frameHeight > 0 ? _frameHeight : 1))
        : (_frameHeight > 0
              ? _frameHeight
              : (_surfaceHeight > 0 ? _surfaceHeight : 1));
    final double logicalW = physW / dpr;
    final double logicalH = physH / dpr;
    return FittedBox(
      fit: BoxFit.contain,
      child: SizedBox(
        width: logicalW,
        height: logicalH,
        child: Texture(textureId: textureId, filterQuality: FilterQuality.none),
      ),
    );
  }

  /// Convert Flutter's button bitmask to engine button index.
  /// Flutter: kPrimaryButton=1, kSecondaryButton=2, kMiddleMouseButton=4
  /// Engine:  0=left, 1=right, 2=middle
  static int _flutterButtonsToEngineButton(int buttons) {
    if (buttons & kSecondaryButton != 0) return 1; // right
    if (buttons & kMiddleMouseButton != 0) return 2; // middle
    return 0; // left (default)
  }

  EngineInputEventData _buildPointerEventData({
    required int type,
    required PointerEvent event,
    double? deltaX,
    double? deltaY,
  }) {
    // Map pointer position from Listener's logical coordinate space
    // to the engine surface's physical pixel coordinates.
    //
    // Listener's localPosition is in logical pixels. The engine's
    // EGL surface is sized in physical pixels (logical * dpr), so
    // multiply by dpr to get surface coordinates.
    //
    // The C++ side (DrawDevice::TransformToPrimaryLayerManager)
    // then maps these surface coordinates → primary layer coordinates.
    final double dpr = _devicePixelRatio > 0 ? _devicePixelRatio : 1.0;
    return EngineInputEventData(
      type: type,
      timestampMicros: event.timeStamp.inMicroseconds,
      x: event.localPosition.dx * dpr,
      y: event.localPosition.dy * dpr,
      deltaX: (deltaX ?? event.delta.dx) * dpr,
      deltaY: (deltaY ?? event.delta.dy) * dpr,
      pointerId: event.pointer,
      button: _flutterButtonsToEngineButton(event.buttons),
    );
  }

  void _sendPointer({
    required int type,
    required PointerEvent event,
    double? deltaX,
    double? deltaY,
  }) {
    if (!widget.active) {
      return;
    }
    unawaited(
      _sendInputEvent(
        _buildPointerEventData(
          type: type,
          event: event,
          deltaX: deltaX,
          deltaY: deltaY,
        ),
      ),
    );
  }

  void _sendCoalescedPointerMove(PointerEvent event) {
    if (!widget.active) {
      return;
    }
    _pendingPointerMoveEvent = _buildPointerEventData(
      type: EngineInputEventType.pointerMove,
      event: event,
    );
    if (_pointerMoveFlushScheduled) {
      return;
    }
    _pointerMoveFlushScheduled = true;
    SchedulerBinding.instance.scheduleFrameCallback((_) {
      _pointerMoveFlushScheduled = false;
      if (!mounted || !widget.active) {
        _pendingPointerMoveEvent = null;
        return;
      }
      final EngineInputEventData? pending = _pendingPointerMoveEvent;
      _pendingPointerMoveEvent = null;
      if (pending != null) {
        unawaited(_sendInputEvent(pending));
      }
    });
  }

  void _ensureSurfaceSizeIfNeeded(Size size, double devicePixelRatio) {
    if (!widget.active || !size.isFinite) {
      return;
    }
    final double normalizedDpr = devicePixelRatio <= 0 ? 1.0 : devicePixelRatio;
    if ((_lastRequestedLogicalSize.width - size.width).abs() < 0.01 &&
        (_lastRequestedLogicalSize.height - size.height).abs() < 0.01 &&
        (_lastRequestedDpr - normalizedDpr).abs() < 0.001) {
      return;
    }
    _lastRequestedLogicalSize = size;
    _lastRequestedDpr = normalizedDpr;
    unawaited(_ensureSurfaceSize(size, normalizedDpr));
  }

  Future<void> _sendInputEvent(EngineInputEventData event) async {
    final int result = await widget.bridge.engineSendInput(event);
    if (result != 0) {
      _reportError(
        'engine_send_input failed: result=$result, error=${widget.bridge.engineGetLastError()}',
      );
    }
  }

  KeyEventResult _onKeyEvent(FocusNode node, KeyEvent event) {
    if (!widget.active) {
      return KeyEventResult.ignored;
    }

    if (Platform.isMacOS &&
        event is KeyDownEvent &&
        HardwareKeyboard.instance.isMetaPressed &&
        event.logicalKey == LogicalKeyboardKey.keyQ) {
      unawaited(_platformChannel.invokeMethod<void>('forceQuitApp'));
      return KeyEventResult.handled;
    }

    if (Platform.isMacOS && HardwareKeyboard.instance.isMetaPressed) {
      return KeyEventResult.ignored;
    }

    final bool isDown = event is KeyDownEvent;
    final bool isUp = event is KeyUpEvent;
    if (!isDown && !isUp) {
      return KeyEventResult.ignored;
    }

    final int type = isDown
        ? EngineInputEventType.keyDown
        : EngineInputEventType.keyUp;
    final int keyCode = event.logicalKey.keyId & 0xFFFFFFFF;
    unawaited(
      _sendInputEvent(
        EngineInputEventData(
          type: type,
          timestampMicros: event.timeStamp.inMicroseconds,
          keyCode: keyCode,
        ),
      ),
    );

    if (isDown &&
        (event.logicalKey == LogicalKeyboardKey.escape ||
            event.logicalKey == LogicalKeyboardKey.goBack)) {
      unawaited(
        _sendInputEvent(
          EngineInputEventData(
            type: EngineInputEventType.back,
            timestampMicros: event.timeStamp.inMicroseconds,
            keyCode: keyCode,
          ),
        ),
      );
    }

    return KeyEventResult.handled;
  }

  @override
  Widget build(BuildContext context) {
    final int? activeTextureId =
        _ioSurfaceTextureId ?? _surfaceTextureId ?? _textureId;

    return LayoutBuilder(
      builder: (BuildContext context, BoxConstraints constraints) {
        final Size size = Size(constraints.maxWidth, constraints.maxHeight);
        final double dpr = MediaQuery.of(context).devicePixelRatio;
        _ensureSurfaceSizeIfNeeded(size, dpr);

        return Focus(
          focusNode: _focusNode,
          autofocus: true,
          onKeyEvent: _onKeyEvent,
          child: Listener(
            behavior: HitTestBehavior.opaque,
            onPointerDown: (event) {
              _focusNode.requestFocus();
              _sendPointer(
                type: EngineInputEventType.pointerDown,
                event: event,
              );
            },
            onPointerMove: (event) {
              _sendCoalescedPointerMove(event);
            },
            onPointerUp: (event) {
              _sendPointer(type: EngineInputEventType.pointerUp, event: event);
            },
            onPointerHover: (event) {
              _sendCoalescedPointerMove(event);
            },
            onPointerSignal: (PointerSignalEvent signal) {
              if (signal is PointerScrollEvent) {
                _sendPointer(
                  type: EngineInputEventType.pointerScroll,
                  event: signal,
                  deltaX: signal.scrollDelta.dx,
                  deltaY: signal.scrollDelta.dy,
                );
              }
            },
            child: Container(
              color: Colors.black,
              child: Stack(
                fit: StackFit.expand,
                children: [
                  if (activeTextureId != null)
                    _buildTextureView(activeTextureId)
                  else if (_frameImage == null)
                    const SizedBox.shrink()
                  else
                    RawImage(
                      image: _frameImage,
                      fit: BoxFit.contain,
                      filterQuality: FilterQuality.none,
                    ),
                ],
              ),
            ),
          ),
        );
      },
    );
  }
}
