import 'package:flutter/material.dart';

/// Shared animation utilities for a smooth, editorial feel.
/// All durations use warm, unhurried timing curves.
class AppAnimations {
  AppAnimations._();

  /// Standard transition duration — 300ms feels warm and intentional.
  static const Duration standard = Duration(milliseconds: 300);

  /// Quick micro-feedback — 150ms for press states.
  static const Duration quick = Duration(milliseconds: 150);

  /// Gentle entrance — 400ms for page/content reveals.
  static const Duration gentle = Duration(milliseconds: 400);

  /// Staggered grid entrance — delay between items.
  static const Duration staggerDelay = Duration(milliseconds: 50);

  /// Warm ease-out curve — decelerates smoothly, slightly longer tail than cubic.
  static const Curve warmEaseOut = Curves.easeOutQuart;

  /// Warm ease-in-out — for reversible transitions.
  static const Curve warmEaseInOut = Curves.easeInOutQuart;

  /// Gentle spring for interactive elements.
  static const Curve gentleSpring = Curves.easeOutCirc;

  /// Cover card press scale factor.
  static const double cardPressScale = 0.96;

  /// Spatial page route (forward: slight scale up + slide up + fade in).
  static Route<T> spatialRoute<T>(Widget page) {
    return PageRouteBuilder<T>(
      pageBuilder: (context, animation, secondaryAnimation) => page,
      transitionDuration: const Duration(milliseconds: 400),
      reverseTransitionDuration: const Duration(milliseconds: 300),
      transitionsBuilder: (context, animation, secondaryAnimation, child) {
        final curved = CurvedAnimation(parent: animation, curve: warmEaseOut);
        return SlideTransition(
          position: Tween<Offset>(
            begin: const Offset(0, 0.02),
            end: Offset.zero,
          ).animate(curved),
          child: ScaleTransition(
            scale: Tween<double>(begin: 0.96, end: 1.0).animate(curved),
            child: FadeTransition(
              opacity: curved,
              child: child,
            ),
          ),
        );
      },
    );
  }

  /// Fade-only route for overlays (settings, dialogs).
  static Route<T> fadeRoute<T>(Widget page) {
    return PageRouteBuilder<T>(
      pageBuilder: (context, animation, secondaryAnimation) => page,
      transitionDuration: Duration(milliseconds: 250),
      reverseTransitionDuration: Duration(milliseconds: 200),
      transitionsBuilder: (context, animation, secondaryAnimation, child) {
        return FadeTransition(
          opacity: CurvedAnimation(parent: animation, curve: warmEaseOut),
          child: child,
        );
      },
    );
  }

  /// Staggered entrance animation wrapper for grid/list items.
  static Widget staggeredEntrance({
    required int index,
    required Widget child,
  }) {
    return TweenAnimationBuilder<double>(
      tween: Tween(begin: 0.0, end: 1.0),
      duration: gentle,
      curve: Interval(
        (index * 0.04).clamp(0.0, 0.6),
        1.0,
        curve: warmEaseOut,
      ),
      builder: (context, value, child) {
        return Opacity(
          opacity: value.clamp(0.0, 1.0),
          child: Transform.translate(
            offset: Offset(0, 12 * (1 - value)),
            child: child,
          ),
        );
      },
      child: child,
    );
  }
}
