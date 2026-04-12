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

enum EngineSurfacePointerMode { directTouch, virtualCursor }

const Size _virtualCursorOverlaySize = Size(48, 48);
const Offset _virtualCursorHotspot = Offset(6, 6);

class _VirtualCursorPainter extends CustomPainter {
  const _VirtualCursorPainter();

  @override
  void paint(Canvas canvas, Size size) {
    final Path arrow = Path()
      ..moveTo(_virtualCursorHotspot.dx, _virtualCursorHotspot.dy)
      ..lineTo(size.width * 0.72, size.height * 0.72)
      ..lineTo(size.width * 0.52, size.height * 0.72)
      ..lineTo(size.width * 0.63, size.height * 0.98)
      ..lineTo(size.width * 0.46, size.height * 0.98)
      ..lineTo(size.width * 0.36, size.height * 0.74)
      ..lineTo(size.width * 0.16, size.height * 0.9)
      ..close();

    canvas.drawShadow(arrow, const Color(0xDD000000), 10, false);

    final Paint outlinePaint = Paint()
      ..color = const Color(0xFF111111)
      ..style = PaintingStyle.stroke
      ..strokeWidth = 2.2
      ..strokeJoin = StrokeJoin.round;
    final Paint fillPaint = Paint()
      ..color = const Color(0xFFF8F8F8)
      ..style = PaintingStyle.fill;

    canvas.drawPath(arrow, fillPaint);
    canvas.drawPath(arrow, outlinePaint);
  }

  @override
  bool shouldRepaint(covariant CustomPainter oldDelegate) => false;
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
    this.onVirtualCursorModeChanged,
    this.onSoftKeyboardVisibilityChanged,
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
  final ValueChanged<bool>? onVirtualCursorModeChanged;
  final ValueChanged<bool>? onSoftKeyboardVisibilityChanged;

  @override
  EngineSurfaceState createState() => EngineSurfaceState();
}

class EngineSurfaceState extends State<EngineSurface> with TextInputClient {
  static const MethodChannel _platformChannel = MethodChannel(
    'flutter_engine_bridge',
  );
  static const int _modifierShift = 1 << 0;
  static const int _modifierAlt = 1 << 1;
  static const int _modifierCtrl = 1 << 2;
  static const int _modifierLeft = 1 << 3;
  static const int _modifierRight = 1 << 4;
  static const int _modifierMiddle = 1 << 5;
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
  TextInputConnection? _textInputConnection;
  TextEditingValue _textEditingValue = TextEditingValue.empty;
  bool _softKeyboardVisible = false;
  EngineSurfacePointerMode _pointerMode = EngineSurfacePointerMode.directTouch;
  Size _surfaceLogicalSize = Size.zero;
  Offset _virtualCursorLogicalPosition = Offset.zero;
  bool _virtualCursorPositionInitialized = false;
  final Set<int> _virtualCursorActivePointers = <int>{};
  int? _virtualCursorPrimaryPointer;
  Offset? _virtualCursorPrimaryPointerStart;
  bool _virtualCursorPossibleRightClick = false;
  Timer? _virtualCursorLongPressTimer;
  bool _virtualCursorDragActive = false;
  bool _virtualCursorMovedDuringGesture = false;
  Future<void>? _shutdownFuture;

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
    _cancelVirtualCursorLongPress();
    _hideSoftKeyboard();
    _frameImage?.dispose();
    unawaited(_disposeAllTextures());
    _focusNode.dispose();
    super.dispose();
  }

  Future<void> prepareForTeardown() {
    final existing = _shutdownFuture;
    if (existing != null) {
      return existing;
    }

    final future = () async {
      _vsyncScheduled = false;
      _cancelVirtualCursorLongPress();
      _hideSoftKeyboard();
      final ui.Image? previousImage = _frameImage;
      _frameImage = null;
      previousImage?.dispose();
      await _disposeAllTextures();
    }();
    _shutdownFuture = future;
    return future;
  }

  bool get isVirtualCursorEnabled =>
      _pointerMode == EngineSurfacePointerMode.virtualCursor;

  bool get isSoftKeyboardVisible => _softKeyboardVisible;

  Future<bool> toggleVirtualCursorMode() async {
    final bool enabled = _pointerMode != EngineSurfacePointerMode.virtualCursor;
    _cancelVirtualCursorLongPress();
    _virtualCursorActivePointers.clear();
    _virtualCursorPrimaryPointer = null;
    _virtualCursorPrimaryPointerStart = null;
    _virtualCursorPossibleRightClick = false;
    if (_virtualCursorDragActive) {
      _virtualCursorDragActive = false;
      unawaited(_sendVirtualCursorButtonEvent(isDown: false, button: 0));
    }
    if (mounted) {
      setState(() {
        _pointerMode = enabled
            ? EngineSurfacePointerMode.virtualCursor
            : EngineSurfacePointerMode.directTouch;
        if (enabled) {
          _placeVirtualCursorAtSurfaceCenter();
        }
      });
    } else {
      _pointerMode = enabled
          ? EngineSurfacePointerMode.virtualCursor
          : EngineSurfacePointerMode.directTouch;
      if (enabled) {
        _placeVirtualCursorAtSurfaceCenter();
      }
    }
    widget.onVirtualCursorModeChanged?.call(enabled);
    _focusNode.requestFocus();
    return enabled;
  }

  Future<bool> toggleSoftKeyboard() async {
    if (!_supportsSoftKeyboard) {
      return false;
    }
    if (_softKeyboardVisible) {
      _hideSoftKeyboard();
    } else {
      _showSoftKeyboard();
    }
    return _softKeyboardVisible;
  }

  bool get _supportsSoftKeyboard => Platform.isAndroid || Platform.isIOS;

  void _showSoftKeyboard() {
    if (!_supportsSoftKeyboard) {
      return;
    }
    _focusNode.requestFocus();
    _ensureTextInputConnection();
    _textInputConnection?.show();
    _setSoftKeyboardVisible(true);
  }

  void _hideSoftKeyboard() {
    _textInputConnection?.close();
    _textInputConnection = null;
    _textEditingValue = TextEditingValue.empty;
    _setSoftKeyboardVisible(false);
  }

  void _ensureTextInputConnection() {
    if (!_supportsSoftKeyboard) {
      return;
    }
    if (_textInputConnection?.attached ?? false) {
      return;
    }
    _textInputConnection = TextInput.attach(
      this,
      const TextInputConfiguration(
        inputType: TextInputType.text,
        inputAction: TextInputAction.done,
        autocorrect: false,
        enableSuggestions: false,
        enableDeltaModel: false,
      ),
    );
    _textInputConnection?.setEditingState(_textEditingValue);
  }

  void _setSoftKeyboardVisible(bool visible) {
    if (_softKeyboardVisible == visible) {
      return;
    }
    _softKeyboardVisible = visible;
    widget.onSoftKeyboardVisibilityChanged?.call(visible);
  }

  void _syncTextEditingValue(TextEditingValue value) {
    _textEditingValue = value;
    if (_textInputConnection?.attached ?? false) {
      _textInputConnection?.setEditingState(value);
    }
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

  int _currentModifierFlags({int buttons = 0}) {
    int modifiers = 0;
    final keyboard = HardwareKeyboard.instance;
    if (keyboard.isShiftPressed) modifiers |= _modifierShift;
    if (keyboard.isAltPressed) modifiers |= _modifierAlt;
    if (keyboard.isControlPressed) modifiers |= _modifierCtrl;
    if (buttons & kPrimaryButton != 0) modifiers |= _modifierLeft;
    if (buttons & kSecondaryButton != 0) modifiers |= _modifierRight;
    if (buttons & kMiddleMouseButton != 0) modifiers |= _modifierMiddle;
    return modifiers;
  }

  void _sendPrintableText(String text, {int? timestampMicros}) {
    for (final int rune in text.runes) {
      if (rune <= 0 || rune > 0xFFFF) {
        continue;
      }
      if (rune < 0x20 && rune != 0x09) {
        continue;
      }
      unawaited(
        _sendInputEvent(
          EngineInputEventData(
            type: EngineInputEventType.textInput,
            timestampMicros:
                timestampMicros ?? DateTime.now().microsecondsSinceEpoch,
            unicodeCodepoint: rune,
          ),
        ),
      );
    }
  }

  void _sendSyntheticKeyTap(int keyCode) {
    final int timestampMicros = DateTime.now().microsecondsSinceEpoch;
    unawaited(
      _sendInputEvent(
        EngineInputEventData(
          type: EngineInputEventType.keyDown,
          timestampMicros: timestampMicros,
          keyCode: keyCode,
          modifiers: _currentModifierFlags(),
        ),
      ),
    );
    unawaited(
      _sendInputEvent(
        EngineInputEventData(
          type: EngineInputEventType.keyUp,
          timestampMicros: timestampMicros,
          keyCode: keyCode,
          modifiers: _currentModifierFlags(),
        ),
      ),
    );
  }

  void _initializeVirtualCursorPosition(Offset fallbackPosition) {
    if (_virtualCursorPositionInitialized) {
      return;
    }
    final Size size = _surfaceLogicalSize;
    if (size.width <= 0 || size.height <= 0) {
      _virtualCursorLogicalPosition = fallbackPosition;
    } else {
      _virtualCursorLogicalPosition = Offset(
        fallbackPosition.dx.clamp(0.0, size.width),
        fallbackPosition.dy.clamp(0.0, size.height),
      );
    }
    _virtualCursorPositionInitialized = true;
  }

  Offset _clampVirtualCursorPosition(Offset position) {
    final Size size = _surfaceLogicalSize;
    if (size.width <= 0 || size.height <= 0) {
      return position;
    }
    return Offset(
      position.dx.clamp(0.0, size.width),
      position.dy.clamp(0.0, size.height),
    );
  }

  void _placeVirtualCursorAtSurfaceCenter() {
    final Size size = _surfaceLogicalSize;
    if (size.width <= 0 || size.height <= 0) {
      _virtualCursorLogicalPosition = Offset.zero;
      _virtualCursorPositionInitialized = false;
      return;
    }
    _virtualCursorLogicalPosition = Offset(size.width / 2, size.height / 2);
    _virtualCursorPositionInitialized = true;
  }

  bool _useVirtualCursorForEvent(PointerEvent event) {
    return _pointerMode == EngineSurfacePointerMode.virtualCursor &&
        (Platform.isAndroid || Platform.isIOS) &&
        event.kind == PointerDeviceKind.touch;
  }

  EngineInputEventData _buildVirtualCursorEvent({
    required int type,
    required int button,
    Offset? delta,
    bool leftButtonDown = false,
  }) {
    final double dpr = _devicePixelRatio > 0 ? _devicePixelRatio : 1.0;
    return EngineInputEventData(
      type: type,
      timestampMicros: DateTime.now().microsecondsSinceEpoch,
      x: _virtualCursorLogicalPosition.dx * dpr,
      y: _virtualCursorLogicalPosition.dy * dpr,
      deltaX: (delta?.dx ?? 0) * dpr,
      deltaY: (delta?.dy ?? 0) * dpr,
      button: button,
      modifiers: _currentModifierFlags(
        buttons: leftButtonDown ? kPrimaryButton : 0,
      ),
    );
  }

  Future<void> _sendVirtualCursorButtonEvent({
    required bool isDown,
    required int button,
  }) {
    return _sendInputEvent(
      _buildVirtualCursorEvent(
        type: isDown
            ? EngineInputEventType.pointerDown
            : EngineInputEventType.pointerUp,
        button: button,
        leftButtonDown: isDown && button == 0,
      ),
    );
  }

  Future<void> _sendVirtualCursorClick({required int button}) async {
    await _sendVirtualCursorButtonEvent(isDown: true, button: button);
    await _sendVirtualCursorButtonEvent(isDown: false, button: button);
  }

  void _cancelVirtualCursorLongPress() {
    _virtualCursorLongPressTimer?.cancel();
    _virtualCursorLongPressTimer = null;
  }

  void _scheduleVirtualCursorLongPress() {
    _cancelVirtualCursorLongPress();
    _virtualCursorLongPressTimer = Timer(const Duration(milliseconds: 280), () {
      if (!_virtualCursorActivePointers.contains(
            _virtualCursorPrimaryPointer,
          ) ||
          _virtualCursorPossibleRightClick ||
          _virtualCursorDragActive) {
        return;
      }
      _virtualCursorDragActive = true;
      unawaited(_sendVirtualCursorButtonEvent(isDown: true, button: 0));
    });
  }

  void _handleVirtualCursorPointerDown(PointerDownEvent event) {
    _focusNode.requestFocus();
    _initializeVirtualCursorPosition(event.localPosition);
    _virtualCursorActivePointers.add(event.pointer);
    if (_virtualCursorActivePointers.length == 1) {
      _virtualCursorPrimaryPointer = event.pointer;
      _virtualCursorPrimaryPointerStart = event.localPosition;
      _virtualCursorPossibleRightClick = false;
      _virtualCursorDragActive = false;
      _virtualCursorMovedDuringGesture = false;
      if (mounted) {
        setState(() {});
      }
      _scheduleVirtualCursorLongPress();
      return;
    }
    if (_virtualCursorActivePointers.length == 2) {
      _cancelVirtualCursorLongPress();
      _virtualCursorPossibleRightClick = true;
    }
  }

  void _handleVirtualCursorPointerMove(PointerMoveEvent event) {
    if (_virtualCursorPrimaryPointer != event.pointer) {
      if (_virtualCursorActivePointers.length >= 2 &&
          event.delta.distanceSquared > 36) {
        _virtualCursorPossibleRightClick = false;
      }
      return;
    }

    final Offset nextPosition = _clampVirtualCursorPosition(
      _virtualCursorLogicalPosition + event.delta,
    );
    final Offset appliedDelta = nextPosition - _virtualCursorLogicalPosition;
    if (appliedDelta == Offset.zero) {
      return;
    }

    _virtualCursorLogicalPosition = nextPosition;
    _virtualCursorMovedDuringGesture = true;
    if (mounted) {
      setState(() {});
    }

    if (!_virtualCursorDragActive &&
        _virtualCursorPrimaryPointerStart != null &&
        (event.localPosition - _virtualCursorPrimaryPointerStart!).distance >
            10) {
      _cancelVirtualCursorLongPress();
      _virtualCursorPossibleRightClick = false;
    }

    unawaited(
      _sendInputEvent(
        _buildVirtualCursorEvent(
          type: EngineInputEventType.pointerMove,
          button: 0,
          delta: appliedDelta,
          leftButtonDown: _virtualCursorDragActive,
        ),
      ),
    );
  }

  void _handleVirtualCursorPointerUp(PointerUpEvent event) {
    _virtualCursorActivePointers.remove(event.pointer);
    if (_virtualCursorPossibleRightClick &&
        _virtualCursorActivePointers.isEmpty &&
        !_virtualCursorDragActive) {
      _virtualCursorPossibleRightClick = false;
      _virtualCursorPrimaryPointer = null;
      _virtualCursorPrimaryPointerStart = null;
      unawaited(_sendVirtualCursorClick(button: 1));
      return;
    }

    if (_virtualCursorPrimaryPointer == event.pointer) {
      _cancelVirtualCursorLongPress();
      if (_virtualCursorDragActive) {
        _virtualCursorDragActive = false;
        unawaited(_sendVirtualCursorButtonEvent(isDown: false, button: 0));
      } else if (_virtualCursorActivePointers.isEmpty &&
          !_virtualCursorMovedDuringGesture) {
        unawaited(_sendVirtualCursorClick(button: 0));
      }
      _virtualCursorPrimaryPointer = null;
      _virtualCursorPrimaryPointerStart = null;
    }

    if (_virtualCursorActivePointers.isEmpty) {
      _virtualCursorPossibleRightClick = false;
    }
  }

  Widget _buildVirtualCursorOverlay() {
    if (!isVirtualCursorEnabled || !_virtualCursorPositionInitialized) {
      return const SizedBox.shrink();
    }
    return Positioned(
      left: _virtualCursorLogicalPosition.dx - _virtualCursorHotspot.dx,
      top: _virtualCursorLogicalPosition.dy - _virtualCursorHotspot.dy,
      child: IgnorePointer(
        child: RepaintBoundary(
          child: CustomPaint(
            size: _virtualCursorOverlaySize,
            painter: const _VirtualCursorPainter(),
          ),
        ),
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
      modifiers: _currentModifierFlags(buttons: event.buttons),
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

  @override
  TextEditingValue get currentTextEditingValue => _textEditingValue;

  @override
  AutofillScope? get currentAutofillScope => null;

  @override
  void updateEditingValue(TextEditingValue value) {
    final TextEditingValue previousValue = _textEditingValue;
    _textEditingValue = value;

    if (value.composing.isValid && !value.composing.isCollapsed) {
      return;
    }

    final String oldText = previousValue.text;
    final String newText = value.text;
    int prefixLength = 0;
    final int maxPrefix = oldText.length < newText.length
        ? oldText.length
        : newText.length;
    while (prefixLength < maxPrefix &&
        oldText.codeUnitAt(prefixLength) == newText.codeUnitAt(prefixLength)) {
      prefixLength++;
    }

    int oldSuffixLength = oldText.length;
    int newSuffixLength = newText.length;
    while (oldSuffixLength > prefixLength &&
        newSuffixLength > prefixLength &&
        oldText.codeUnitAt(oldSuffixLength - 1) ==
            newText.codeUnitAt(newSuffixLength - 1)) {
      oldSuffixLength--;
      newSuffixLength--;
    }

    final int removedCount = oldSuffixLength - prefixLength;
    final String insertedText = newText.substring(
      prefixLength,
      newSuffixLength,
    );
    for (int i = 0; i < removedCount; i++) {
      _sendSyntheticKeyTap(0x08);
    }
    if (insertedText.isNotEmpty) {
      _sendPrintableText(insertedText);
    }
  }

  @override
  void performAction(TextInputAction action) {
    _sendSyntheticKeyTap(0x0D);
  }

  @override
  void performPrivateCommand(String action, Map<String, dynamic> data) {}

  @override
  void updateFloatingCursor(RawFloatingCursorPoint point) {}

  @override
  void showAutocorrectionPromptRect(int start, int end) {}

  @override
  void connectionClosed() {
    _textInputConnection = null;
    _syncTextEditingValue(TextEditingValue.empty);
    _setSoftKeyboardVisible(false);
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
    final int modifiers = _currentModifierFlags();
    unawaited(
      _sendInputEvent(
        EngineInputEventData(
          type: type,
          timestampMicros: event.timeStamp.inMicroseconds,
          keyCode: keyCode,
          modifiers: modifiers,
        ),
      ),
    );

    final String? character = event.character;
    if (isDown &&
        character != null &&
        character.isNotEmpty &&
        !_isControlCharacter(character)) {
      _sendPrintableText(
        character,
        timestampMicros: event.timeStamp.inMicroseconds,
      );
    }

    if (isDown &&
        (event.logicalKey == LogicalKeyboardKey.escape ||
            event.logicalKey == LogicalKeyboardKey.goBack)) {
      unawaited(
        _sendInputEvent(
          EngineInputEventData(
            type: EngineInputEventType.back,
            timestampMicros: event.timeStamp.inMicroseconds,
            keyCode: keyCode,
            modifiers: modifiers,
          ),
        ),
      );
    }

    return KeyEventResult.handled;
  }

  bool _isControlCharacter(String character) {
    if (character.isEmpty) {
      return true;
    }
    final int codePoint = character.runes.first;
    return codePoint < 0x20 || codePoint == 0x7F;
  }

  @override
  Widget build(BuildContext context) {
    final int? activeTextureId =
        _ioSurfaceTextureId ?? _surfaceTextureId ?? _textureId;

    return LayoutBuilder(
      builder: (BuildContext context, BoxConstraints constraints) {
        final Size size = Size(constraints.maxWidth, constraints.maxHeight);
        _surfaceLogicalSize = size;
        if (isVirtualCursorEnabled && !_virtualCursorPositionInitialized) {
          _placeVirtualCursorAtSurfaceCenter();
        }
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
              if (_useVirtualCursorForEvent(event)) {
                _handleVirtualCursorPointerDown(event);
                return;
              }
              _sendPointer(
                type: EngineInputEventType.pointerDown,
                event: event,
              );
            },
            onPointerMove: (event) {
              if (_useVirtualCursorForEvent(event)) {
                _handleVirtualCursorPointerMove(event);
                return;
              }
              _sendCoalescedPointerMove(event);
            },
            onPointerUp: (event) {
              if (_useVirtualCursorForEvent(event)) {
                _handleVirtualCursorPointerUp(event);
                return;
              }
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
                  _buildVirtualCursorOverlay(),
                ],
              ),
            ),
          ),
        );
      },
    );
  }
}
