import 'package:flutter/gestures.dart';
import 'package:flutter/material.dart';
import '../theme/app_animations.dart';

class App3DTiltHover extends StatefulWidget {
  final Widget child;
  final double maxTiltX;
  final double maxTiltY;
  final double hoverScale;
  final VoidCallback? onTap;
  final VoidCallback? onLongPress;
  final VoidCallback? onSecondaryTap;

  const App3DTiltHover({
    super.key,
    required this.child,
    this.maxTiltX = 0.12, // subtle 3D tilt
    this.maxTiltY = 0.12,
    this.hoverScale = 1.03,
    this.onTap,
    this.onLongPress,
    this.onSecondaryTap,
  });

  @override
  State<App3DTiltHover> createState() => _App3DTiltHoverState();
}

class _App3DTiltHoverState extends State<App3DTiltHover> {
  double xAlignment = 0;
  double yAlignment = 0;
  bool isHovered = false;
  bool isPressed = false;

  void _onHover(PointerEvent event, Size size) {
    if (size.width == 0 || size.height == 0) return;
    final x = (event.localPosition.dx / size.width) * 2 - 1;
    final y = (event.localPosition.dy / size.height) * 2 - 1;
    setState(() {
      xAlignment = x.clamp(-1.0, 1.0);
      yAlignment = y.clamp(-1.0, 1.0);
      isHovered = true;
    });
  }

  void _onExit(PointerEvent event) {
    setState(() {
      xAlignment = 0;
      yAlignment = 0;
      isHovered = false;
    });
  }

  @override
  Widget build(BuildContext context) {
    return LayoutBuilder(
      builder: (context, constraints) {
        final size = Size(constraints.maxWidth, constraints.maxHeight);
        
        var scale = 1.0;
        if (isPressed) {
          scale = AppAnimations.cardPressScale;
        } else if (isHovered) {
          scale = widget.hoverScale;
        }

        final transform = Matrix4.identity()..setEntry(3, 2, 0.001);
        if (isHovered && !isPressed) {
          transform
            ..rotateX(-yAlignment * widget.maxTiltX)
            ..rotateY(xAlignment * widget.maxTiltY);
        }
        transform.scale(scale, scale, 1.0);

        return MouseRegion(
          onHover: (e) => _onHover(e, size),
          onExit: _onExit,
          cursor: widget.onTap != null ? SystemMouseCursors.click : MouseCursor.defer,
          child: GestureDetector(
            behavior: HitTestBehavior.opaque,
            onTapDown: (_) => setState(() => isPressed = true),
            onTapUp: (_) {
              setState(() => isPressed = false);
              widget.onTap?.call();
            },
            onTapCancel: () => setState(() => isPressed = false),
            onSecondaryTap: widget.onSecondaryTap,
            onLongPress: widget.onLongPress,
            child: TweenAnimationBuilder<Matrix4>(
              tween: Tween<Matrix4>(end: transform),
              duration: isPressed ? AppAnimations.quick : const Duration(milliseconds: 250),
              curve: AppAnimations.warmEaseOut,
              builder: (context, matrix, child) {
                return Transform(
                  transform: matrix,
                  alignment: Alignment.center,
                  child: Container(
                    decoration: isHovered && !isPressed ? BoxDecoration(
                      boxShadow: [
                        BoxShadow(
                          color: Theme.of(context).colorScheme.shadow.withValues(alpha: 0.25),
                          blurRadius: 24,
                          spreadRadius: 2,
                          offset: Offset(xAlignment * -8, yAlignment * -8 + 12),
                        )
                      ]
                    ) : null,
                    child: child,
                  ),
                );
              },
              child: widget.child,
            ),
          ),
        );
      },
    );
  }
}
