import 'package:flutter/material.dart';

/// Design system tokens inspired by warm, editorial aesthetics.
/// All colors carry a yellow-brown (warm) undertone — no cool blue-grays.
class AppColors {
  // ── Brand ──
  static const Color terracottaBrand = Color(0xFFC96442);
  static const Color coralAccent = Color(0xFFD97757);

  // ── Error / Focus ──
  static const Color errorCrimson = Color(0xFFB53333);
  static const Color focusBlue = Color(0xFF3898EC);

  // ── Light surfaces ──
  static const Color parchment = Color(0xFFF5F4ED);
  static const Color ivory = Color(0xFFFAF9F5);
  static const Color warmSand = Color(0xFFE8E6DC);

  // ── Dark surfaces ──
  static const Color deepDark = Color(0xFF141413);
  static const Color darkSurface = Color(0xFF30302E);

  // ── Text ──
  static const Color nearBlack = Color(0xFF141413);
  static const Color charcoalWarm = Color(0xFF4D4C48);
  static const Color oliveGray = Color(0xFF5E5D59);
  static const Color stoneGray = Color(0xFF87867F);
  static const Color darkWarm = Color(0xFF3D3D3A);
  static const Color warmSilver = Color(0xFFB0AEA5);

  // ── Borders / Rings ──
  static const Color borderCream = Color(0xFFF0EEE6);
  static const Color borderWarm = Color(0xFFE8E6DC);
  static const Color ringWarm = Color(0xFFD1CFC5);
  static const Color ringSubtle = Color(0xFFDEDCC5);
  static const Color ringDeep = Color(0xFFC2C0B6);
}

/// Light theme — Parchment canvas with warm tones.
ThemeData buildLightTheme() {
  final colorScheme = ColorScheme(
    brightness: Brightness.light,
    primary: AppColors.terracottaBrand,
    onPrimary: AppColors.ivory,
    primaryContainer: const Color(0xFFE8C4B0),
    onPrimaryContainer: AppColors.nearBlack,
    secondary: AppColors.warmSand,
    onSecondary: AppColors.charcoalWarm,
    secondaryContainer: const Color(0xFFF0EDE3),
    onSecondaryContainer: AppColors.charcoalWarm,
    tertiary: AppColors.coralAccent,
    onTertiary: AppColors.ivory,
    error: AppColors.errorCrimson,
    onError: AppColors.ivory,
    errorContainer: const Color(0xFFFFDAD6),
    onErrorContainer: AppColors.nearBlack,
    surface: AppColors.parchment,
    onSurface: AppColors.nearBlack,
    surfaceContainerHighest: AppColors.warmSand,
    surfaceContainerHigh: const Color(0xFFEDEBE3),
    surfaceContainerLow: AppColors.ivory,
    surfaceContainer: const Color(0xFFF0EEE6),
    onSurfaceVariant: AppColors.oliveGray,
    outline: AppColors.stoneGray,
    outlineVariant: AppColors.ringWarm,
    shadow: AppColors.nearBlack,
    scrim: AppColors.nearBlack,
    inverseSurface: AppColors.deepDark,
    onInverseSurface: AppColors.warmSilver,
  );

  return _buildTheme(colorScheme, Brightness.light);
}

/// Dark theme — Near-black canvas with warm undertones.
ThemeData buildDarkTheme() {
  final colorScheme = ColorScheme(
    brightness: Brightness.dark,
    primary: AppColors.coralAccent,
    onPrimary: AppColors.nearBlack,
    primaryContainer: AppColors.terracottaBrand,
    onPrimaryContainer: AppColors.ivory,
    secondary: AppColors.darkSurface,
    onSecondary: AppColors.warmSilver,
    secondaryContainer: const Color(0xFF3D3D3A),
    onSecondaryContainer: AppColors.warmSilver,
    tertiary: AppColors.coralAccent,
    onTertiary: AppColors.nearBlack,
    error: const Color(0xFFE57373),
    onError: AppColors.nearBlack,
    errorContainer: AppColors.errorCrimson,
    onErrorContainer: AppColors.ivory,
    surface: AppColors.deepDark,
    onSurface: AppColors.warmSilver,
    surfaceContainerHighest: const Color(0xFF3D3D3A),
    surfaceContainerHigh: AppColors.darkSurface,
    surfaceContainerLow: const Color(0xFF222221),
    surfaceContainer: const Color(0xFF282827),
    onSurfaceVariant: AppColors.warmSilver,
    outline: const Color(0xFF6E6D68),
    outlineVariant: AppColors.darkSurface,
    shadow: Colors.black,
    scrim: Colors.black,
    inverseSurface: AppColors.parchment,
    onInverseSurface: AppColors.nearBlack,
  );

  return _buildTheme(colorScheme, Brightness.dark);
}

ThemeData _buildTheme(ColorScheme colorScheme, Brightness brightness) {
  final isLight = brightness == Brightness.light;
  final borderColor = isLight ? AppColors.borderCream : AppColors.darkSurface;

  return ThemeData(
    colorScheme: colorScheme,
    brightness: brightness,
    useMaterial3: true,

    // ── Scaffold ──
    scaffoldBackgroundColor: colorScheme.surface,

    // ── AppBar ──
    appBarTheme: AppBarTheme(
      backgroundColor: colorScheme.surface,
      foregroundColor: colorScheme.onSurface,
      elevation: 0,
      scrolledUnderElevation: 0,
      surfaceTintColor: Colors.transparent,
    ),

    // ── Cards ──
    cardTheme: CardThemeData(
      elevation: 0,
      color: colorScheme.surfaceContainerLow,
      shape: RoundedRectangleBorder(
        borderRadius: BorderRadius.circular(12),
        side: BorderSide(color: borderColor, width: 1),
      ),
    ),

    // ── Elevated / shadow ──
    elevatedButtonTheme: ElevatedButtonThemeData(
      style: ElevatedButton.styleFrom(
        backgroundColor: colorScheme.secondary,
        foregroundColor: colorScheme.onSecondary,
        elevation: 0,
        shadowColor: Colors.transparent,
        shape: RoundedRectangleBorder(
          borderRadius: BorderRadius.circular(12),
        ),
        padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 12),
      ),
    ),

    // ── Filled buttons — Terracotta for primary CTA ──
    filledButtonTheme: FilledButtonThemeData(
      style: FilledButton.styleFrom(
        backgroundColor: colorScheme.primary,
        foregroundColor: colorScheme.onPrimary,
        elevation: 0,
        shape: RoundedRectangleBorder(
          borderRadius: BorderRadius.circular(12),
        ),
        padding: const EdgeInsets.symmetric(horizontal: 20, vertical: 14),
        textStyle: const TextStyle(
          fontSize: 15,
          fontWeight: FontWeight.w500,
          letterSpacing: 0.12,
        ),
      ),
    ),

    // ── Outlined buttons ──
    outlinedButtonTheme: OutlinedButtonThemeData(
      style: OutlinedButton.styleFrom(
        foregroundColor: colorScheme.onSurface,
        shape: RoundedRectangleBorder(
          borderRadius: BorderRadius.circular(12),
        ),
        side: BorderSide(color: colorScheme.outlineVariant),
        padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 12),
      ),
    ),

    // ── Text buttons ──
    textButtonTheme: TextButtonThemeData(
      style: TextButton.styleFrom(
        foregroundColor: colorScheme.primary,
        shape: RoundedRectangleBorder(
          borderRadius: BorderRadius.circular(8),
        ),
        padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
      ),
    ),

    // ── Floating Action Button ──
    floatingActionButtonTheme: FloatingActionButtonThemeData(
      backgroundColor: colorScheme.primary,
      foregroundColor: colorScheme.onPrimary,
      elevation: 0,
      highlightElevation: 0,
      shape: RoundedRectangleBorder(
        borderRadius: BorderRadius.circular(16),
      ),
    ),

    // ── Input decoration ──
    inputDecorationTheme: InputDecorationTheme(
      filled: true,
      fillColor: colorScheme.surfaceContainer,
      border: OutlineInputBorder(
        borderRadius: BorderRadius.circular(12),
        borderSide: BorderSide(color: borderColor),
      ),
      enabledBorder: OutlineInputBorder(
        borderRadius: BorderRadius.circular(12),
        borderSide: BorderSide(color: borderColor),
      ),
      focusedBorder: OutlineInputBorder(
        borderRadius: BorderRadius.circular(12),
        borderSide: const BorderSide(color: AppColors.focusBlue, width: 2),
      ),
      contentPadding: const EdgeInsets.symmetric(horizontal: 12, vertical: 10),
      hintStyle: TextStyle(
        color: colorScheme.onSurface.withValues(alpha: 0.4),
      ),
    ),

    // ── Dividers ──
    dividerTheme: DividerThemeData(
      color: borderColor,
      thickness: 1,
      space: 1,
    ),

    // ── List tiles ──
    listTileTheme: ListTileThemeData(
      shape: RoundedRectangleBorder(
        borderRadius: BorderRadius.circular(8),
      ),
    ),

    // ── Dialogs ──
    dialogTheme: DialogThemeData(
      backgroundColor: colorScheme.surfaceContainerLow,
      shape: RoundedRectangleBorder(
        borderRadius: BorderRadius.circular(16),
        side: BorderSide(color: borderColor),
      ),
      elevation: 0,
    ),

    // ── Snack bars ──
    snackBarTheme: SnackBarThemeData(
      behavior: SnackBarBehavior.floating,
      shape: RoundedRectangleBorder(
        borderRadius: BorderRadius.circular(12),
      ),
      elevation: 0,
    ),

    // ── Bottom sheets ──
    bottomSheetTheme: BottomSheetThemeData(
      backgroundColor: colorScheme.surface,
      shape: const RoundedRectangleBorder(
        borderRadius: BorderRadius.vertical(top: Radius.circular(24)),
      ),
      showDragHandle: false,
    ),

    // ── Popup menu ──
    popupMenuTheme: PopupMenuThemeData(
      color: colorScheme.surfaceContainerLow,
      shape: RoundedRectangleBorder(
        borderRadius: BorderRadius.circular(12),
        side: BorderSide(color: borderColor),
      ),
      elevation: 0,
    ),

    // ── Segmented buttons ──
    segmentedButtonTheme: SegmentedButtonThemeData(
      style: ButtonStyle(
        shape: WidgetStateProperty.resolveWith((states) {
          return RoundedRectangleBorder(
            borderRadius: BorderRadius.circular(8),
          );
        }),
        side: WidgetStateProperty.resolveWith((states) {
          return BorderSide(color: colorScheme.outlineVariant);
        }),
      ),
    ),

    // ── Dropdown ──
    dropdownMenuTheme: DropdownMenuThemeData(
      inputDecorationTheme: InputDecorationTheme(
        filled: true,
        fillColor: colorScheme.surfaceContainer,
        border: OutlineInputBorder(
          borderRadius: BorderRadius.circular(12),
          borderSide: BorderSide(color: borderColor),
        ),
      ),
    ),

    // ── Switch ──
    switchTheme: SwitchThemeData(
      thumbColor: WidgetStateProperty.resolveWith((states) {
        if (states.contains(WidgetState.selected)) {
          return colorScheme.primary;
        }
        return colorScheme.outline;
      }),
      trackColor: WidgetStateProperty.resolveWith((states) {
        if (states.contains(WidgetState.selected)) {
          return colorScheme.primary.withValues(alpha: 0.4);
        }
        return colorScheme.surfaceContainerHighest;
      }),
    ),

    // ── Progress indicator ──
    progressIndicatorTheme: ProgressIndicatorThemeData(
      color: colorScheme.primary,
      linearTrackColor: colorScheme.surfaceContainerHighest,
    ),

    // ── Circular Progress Indicator ──
    // (uses progressIndicatorTheme color)

    // ── Tooltip ──
    tooltipTheme: TooltipThemeData(
      decoration: BoxDecoration(
        color: colorScheme.inverseSurface,
        borderRadius: BorderRadius.circular(8),
      ),
      textStyle: TextStyle(
        color: colorScheme.onInverseSurface,
        fontSize: 12,
      ),
    ),

    // ── Icon ──
    iconTheme: IconThemeData(
      color: colorScheme.onSurfaceVariant,
      size: 24,
    ),

    // ── Typography ──
    // Serif (Georgia) for display/headline/titleLarge/titleMedium — weight 500, tight line-heights
    // Sans (system) for body/label — relaxed line-heights (1.6)
    textTheme: TextTheme(
      // ── Serif headlines ──
      displayLarge: TextStyle(
        fontFamily: 'Georgia',
        fontSize: 32,
        fontWeight: FontWeight.w500,
        height: 1.10,
        color: colorScheme.onSurface,
      ),
      displayMedium: TextStyle(
        fontFamily: 'Georgia',
        fontSize: 28,
        fontWeight: FontWeight.w500,
        height: 1.15,
        color: colorScheme.onSurface,
      ),
      displaySmall: TextStyle(
        fontFamily: 'Georgia',
        fontSize: 24,
        fontWeight: FontWeight.w500,
        height: 1.20,
        color: colorScheme.onSurface,
      ),
      headlineLarge: TextStyle(
        fontFamily: 'Georgia',
        fontSize: 22,
        fontWeight: FontWeight.w500,
        height: 1.20,
        color: colorScheme.onSurface,
      ),
      headlineMedium: TextStyle(
        fontFamily: 'Georgia',
        fontSize: 20,
        fontWeight: FontWeight.w500,
        height: 1.20,
        color: colorScheme.onSurface,
      ),
      headlineSmall: TextStyle(
        fontFamily: 'Georgia',
        fontSize: 18,
        fontWeight: FontWeight.w500,
        height: 1.25,
        color: colorScheme.onSurface,
      ),
      titleLarge: TextStyle(
        fontFamily: 'Georgia',
        fontSize: 16,
        fontWeight: FontWeight.w500,
        height: 1.25,
        color: colorScheme.onSurface,
      ),
      titleMedium: TextStyle(
        fontFamily: 'Georgia',
        fontSize: 14,
        fontWeight: FontWeight.w500,
        height: 1.30,
        color: colorScheme.onSurface,
      ),
      // ── Sans body / UI ──
      titleSmall: TextStyle(
        fontSize: 14,
        fontWeight: FontWeight.w500,
        height: 1.43,
        color: colorScheme.onSurface,
      ),
      bodyLarge: TextStyle(
        fontSize: 16,
        fontWeight: FontWeight.w400,
        height: 1.60,
        color: colorScheme.onSurface,
      ),
      bodyMedium: TextStyle(
        fontSize: 14,
        fontWeight: FontWeight.w400,
        height: 1.60,
        color: colorScheme.onSurface,
      ),
      bodySmall: TextStyle(
        fontSize: 12,
        fontWeight: FontWeight.w400,
        height: 1.43,
        color: colorScheme.onSurface,
      ),
      labelLarge: TextStyle(
        fontSize: 14,
        fontWeight: FontWeight.w500,
        height: 1.43,
        letterSpacing: 0.12,
        color: colorScheme.onSurface,
      ),
      labelMedium: TextStyle(
        fontSize: 12,
        fontWeight: FontWeight.w500,
        height: 1.25,
        letterSpacing: 0.12,
        color: colorScheme.onSurface,
      ),
      labelSmall: TextStyle(
        fontSize: 10,
        fontWeight: FontWeight.w400,
        height: 1.60,
        letterSpacing: 0.5,
        color: colorScheme.onSurface,
      ),
    ),
  );
}
