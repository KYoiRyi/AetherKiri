import 'package:flutter/material.dart';

import '../theme/app_animations.dart';

class AppSlidingSegmentedControl<T> extends StatelessWidget {
  const AppSlidingSegmentedControl({
    super.key,
    required this.value,
    required this.segments,
    required this.onChanged,
  });

  final T value;
  final Map<T, Widget> segments;
  final ValueChanged<T> onChanged;

  @override
  Widget build(BuildContext context) {
    final colorScheme = Theme.of(context).colorScheme;
    final keys = segments.keys.toList();
    final selectedIndex = keys.indexOf(value);
    
    // Fallback if value isn't found
    final safeSelectedIndex = selectedIndex >= 0 ? selectedIndex : 0;

    return LayoutBuilder(
      builder: (context, constraints) {
        final totalWidth = constraints.maxWidth;
        final count = keys.length;
        final itemWidth = count > 0 ? totalWidth / count : 0.0;

        return Container(
          height: 44,
          decoration: BoxDecoration(
            color: colorScheme.surfaceContainerHighest,
            borderRadius: BorderRadius.circular(22),
          ),
          child: Stack(
            clipBehavior: Clip.none,
            children: [
              AnimatedPositioned(
                duration: AppAnimations.gentle,
                curve: AppAnimations.warmEaseOut,
                top: 4,
                bottom: 4,
                left: totalWidth > 0 ? 4 + safeSelectedIndex * itemWidth : 4,
                width: totalWidth > 0 ? itemWidth - 8 : 0,
                child: Container(
                  decoration: BoxDecoration(
                    color: colorScheme.primaryContainer,
                    borderRadius: BorderRadius.circular(18),
                    boxShadow: [
                      BoxShadow(
                        color: colorScheme.shadow.withValues(alpha: 0.1),
                        blurRadius: 4,
                        offset: const Offset(0, 2),
                      ),
                    ],
                  ),
                ),
              ),
              Row(
                children: keys.map((key) {
                  final isSelected = key == value;
                  return Expanded(
                    child: GestureDetector(
                      behavior: HitTestBehavior.opaque,
                      onTap: () => onChanged(key),
                      child: Container(
                        alignment: Alignment.center,
                        child: DefaultTextStyle(
                          style: TextStyle(
                            color: isSelected
                                ? colorScheme.onPrimaryContainer
                                : colorScheme.onSurfaceVariant,
                            fontWeight: isSelected ? FontWeight.w600 : FontWeight.w500,
                            fontSize: 14,
                          ),
                          child: IconTheme(
                            data: IconThemeData(
                              size: 18,
                              color: isSelected
                                  ? colorScheme.onPrimaryContainer
                                  : colorScheme.onSurfaceVariant,
                            ),
                            child: segments[key]!,
                          ),
                        ),
                      ),
                    ),
                  );
                }).toList(),
              ),
            ],
          ),
        );
      },
    );
  }
}
