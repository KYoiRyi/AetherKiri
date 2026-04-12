import 'package:flutter/material.dart';
import '../theme/app_animations.dart';

/// 1. 空状态呼吸悬浮动画
class AppLoopingFloat extends StatefulWidget {
  final Widget child;
  final double offsetAmount;
  final Duration duration;

  const AppLoopingFloat({
    super.key,
    required this.child,
    this.offsetAmount = 8.0,
    this.duration = const Duration(seconds: 2),
  });

  @override
  State<AppLoopingFloat> createState() => _AppLoopingFloatState();
}

class _AppLoopingFloatState extends State<AppLoopingFloat>
    with SingleTickerProviderStateMixin {
  late final AnimationController _controller;

  @override
  void initState() {
    super.initState();
    _controller = AnimationController(
      vsync: this,
      duration: widget.duration,
    )..repeat(reverse: true);
  }

  @override
  void dispose() {
    _controller.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return AnimatedBuilder(
      animation: _controller,
      builder: (context, child) {
        final val = CurvedAnimation(
          parent: _controller,
          curve: Curves.easeInOutSine,
        ).value;
        return Transform.translate(
          offset: Offset(0, -widget.offsetAmount * val),
          child: child,
        );
      },
      child: widget.child,
    );
  }
}

/// 2. 呼吸发光按钮（带按下微缩效果）
class AppBreathingButton extends StatefulWidget {
  final VoidCallback onPressed;
  final Widget icon;
  final Widget label;

  const AppBreathingButton({
    super.key,
    required this.onPressed,
    required this.icon,
    required this.label,
  });

  @override
  State<AppBreathingButton> createState() => _AppBreathingButtonState();
}

class _AppBreathingButtonState extends State<AppBreathingButton>
    with SingleTickerProviderStateMixin {
  late final AnimationController _glowController;
  double _scale = 1.0;

  @override
  void initState() {
    super.initState();
    _glowController = AnimationController(
      vsync: this,
      duration: const Duration(milliseconds: 1500),
    )..repeat(reverse: true);
  }

  @override
  void dispose() {
    _glowController.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final colorScheme = Theme.of(context).colorScheme;

    return GestureDetector(
      onTapDown: (_) => setState(() => _scale = AppAnimations.cardPressScale),
      onTapUp: (_) {
        setState(() => _scale = 1.0);
        widget.onPressed();
      },
      onTapCancel: () => setState(() => _scale = 1.0),
      child: AnimatedScale(
        scale: _scale,
        duration: AppAnimations.quick,
        curve: AppAnimations.warmEaseOut,
        child: AnimatedBuilder(
          animation: _glowController,
          builder: (context, child) {
            final val = CurvedAnimation(parent: _glowController, curve: Curves.easeInOutSine).value;
            return Container(
              decoration: BoxDecoration(
                borderRadius: BorderRadius.circular(16),
                boxShadow: [
                  BoxShadow(
                    color: colorScheme.primary.withValues(alpha: 0.15 + 0.25 * val),
                    blurRadius: 12 + 12 * val,
                    spreadRadius: 2 * val,
                    offset: const Offset(0, 4),
                  )
                ],
              ),
              child: child,
            );
          },
          child: FilledButton.icon(
            onPressed: null, // Gesture handled by outer detector
            icon: widget.icon,
            label: widget.label,
            style: FilledButton.styleFrom(
              disabledBackgroundColor: colorScheme.primary, // override disabled colors
              disabledForegroundColor: colorScheme.onPrimary,
              minimumSize: const Size.fromHeight(52),
              shape: RoundedRectangleBorder(
                borderRadius: BorderRadius.circular(16),
              ),
              textStyle: const TextStyle(fontSize: 16, fontWeight: FontWeight.w600),
            ),
          ),
        ),
      ),
    );
  }
}

/// 3. 灵动回弹的菜单选项磁贴，去除了涟漪，加入了微缩反馈
class AppBouncingTile extends StatefulWidget {
  final Widget leading;
  final Widget title;
  final Widget? trailing;
  final VoidCallback onTap;
  final Color? color;

  const AppBouncingTile({
    super.key,
    required this.leading,
    required this.title,
    this.trailing,
    required this.onTap,
    this.color,
  });

  @override
  State<AppBouncingTile> createState() => _AppBouncingTileState();
}

class _AppBouncingTileState extends State<AppBouncingTile> {
  double _scale = 1.0;
  bool _hovered = false;

  @override
  Widget build(BuildContext context) {
    final contentColor = widget.color ?? Theme.of(context).colorScheme.onSurface;

    return MouseRegion(
      onEnter: (_) => setState(() => _hovered = true),
      onExit: (_) => setState(() => _hovered = false),
      cursor: SystemMouseCursors.click,
      child: GestureDetector(
        behavior: HitTestBehavior.opaque,
        onTapDown: (_) => setState(() => _scale = 0.96),
        onTapUp: (_) {
          setState(() => _scale = 1.0);
          widget.onTap();
        },
        onTapCancel: () => setState(() => _scale = 1.0),
        child: AnimatedScale(
          scale: _scale,
          duration: AppAnimations.quick,
          curve: AppAnimations.warmEaseOut,
          child: AnimatedContainer(
            duration: AppAnimations.quick,
            curve: AppAnimations.warmEaseOut,
            decoration: BoxDecoration(
              color: _hovered ? contentColor.withValues(alpha: 0.04) : Colors.transparent,
              borderRadius: BorderRadius.circular(12),
            ),
            padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 14),
            child: Row(
              children: [
                IconTheme(
                  data: IconThemeData(color: contentColor, size: 24),
                  child: widget.leading,
                ),
                const SizedBox(width: 16),
                Expanded(
                  child: DefaultTextStyle(
                    style: Theme.of(context).textTheme.bodyLarge!.copyWith(
                          color: contentColor,
                        ),
                    child: widget.title,
                  ),
                ),
                if (widget.trailing != null)
                  IconTheme(
                    data: IconThemeData(color: contentColor, size: 24),
                    child: widget.trailing!,
                  ),
              ],
            ),
          ),
        ),
      ),
    );
  }
}
