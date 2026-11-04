/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

import 'package:adaptive_platform_ui/adaptive_platform_ui.dart';
import 'package:cupertino_ui/cupertino_ui.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:liquid_glass_widgets/liquid_glass_widgets.dart';
import 'package:stack_chan/util/blue_util.dart';
import 'package:stack_chan/view/util/popup.dart';
import 'package:stack_chan/view/util/unfocus_on_tap_outside.dart';

import '../util/app_toast.dart';
import 'home/home.dart';

class App extends StatefulWidget {
  static const isRelease = false;

  static final GlobalKey<NavigatorState> navigatorKey = GlobalKey();

  static BuildContext appContext() {
    return navigatorKey.currentState!.context;
  }

  static void showDialog(String title) {
    showCupertinoDialog(
      context: appContext(),
      builder: (context) {
        return CupertinoAlertDialog(
          title: Text(title),
          actions: [
            CupertinoDialogAction(
              isDefaultAction: true,
              child: const Text("Confirm"),
              onPressed: () {
                Navigator.maybePop(context);
              },
            ),
          ],
        );
      },
    );
  }

  static Future<T?> showAppSheet<T>(
    WidgetBuilder builder, {
    bool useNestedNavigation = false,
    bool enableDrag = true,
    bool showDragHandle = false,
  }) async {
    return showCustomCupertinoSheet(
      context: appContext(),
      scrollableBuilder: (context, scrollController) {
        return builder(context);
      },
      useNestedNavigation: useNestedNavigation,
      enableDrag: enableDrag,
      showDragHandle: showDragHandle,
    );
  }

  static LiquidGlassSettings glassMenuThemeSettings(BuildContext context) {
    return LiquidGlassSettings(
      glassColor: CupertinoColors.secondarySystemGroupedBackground
          .resolveFrom(context)
          .withValues(alpha: 0.9),
      blur: 3,
    );
  }

  static LiquidGlassSettings glassSegmentedControlThemeSettings(
    BuildContext context,
  ) {
    bool isLight = CupertinoTheme.maybeBrightnessOf(context) == .light;
    if (isLight) {
      return const LiquidGlassSettings(
        glassColor: CupertinoColors.transparent,
        saturation: 0,
        refractiveIndex: 0,
        thickness: 0,
        lightIntensity: 0,
        chromaticAberration: 0,
        blur: 0,
      );
    } else {
      return const LiquidGlassSettings(
        glassColor: CupertinoColors.transparent,
        saturation: 0,
        refractiveIndex: 0,
        thickness: 0,
        lightIntensity: 0,
        chromaticAberration: 0,
        blur: 0,
      );
    }
  }

  static LiquidGlassSettings glassIconButtonThemeSettings(
    BuildContext context,
    Color? glassColor,
  ) {
    return LiquidGlassSettings(
      glassColor:
          glassColor ??
          CupertinoColors.secondarySystemGroupedBackground
              .resolveFrom(context)
              .withValues(alpha: 0.9),
      blur: 3,
    );
  }

  static Color glassSegmentedControlColor(BuildContext context) {
    bool isLight = CupertinoTheme.maybeBrightnessOf(context) == .light;
    if (isLight) {
      return CupertinoColors.white;
    } else {
      return CupertinoColors.systemGrey3.resolveFrom(context);
    }
  }

  static LiquidGlassSettings glassBottomBarThemeSettings(BuildContext context) {
    bool isLight = CupertinoTheme.maybeBrightnessOf(context) == .light;
    if (isLight) {
      return LiquidGlassSettings.ios27Light;
    } else {
      return LiquidGlassSettings.ios27Dark;
    }
  }

  static final RouteObserver<PageRoute> routeObserver =
      RouteObserver<PageRoute>();

  static LiquidGlassSettings glassGroupedSectionSettings(BuildContext context) {
    return LiquidGlassSettings(
      glassColor: CupertinoColors.secondarySystemGroupedBackground.resolveFrom(
        context,
      ),
      visibility: 1.0,
      thickness: 0,
      blur: 0,
      chromaticAberration: 0.0,
      lightAngle: 0,
      lightIntensity: 0.0,
      ambientStrength: 0.0,
      refractiveIndex: 1.0,
      saturation: 1.0,
      glowIntensity: 0.0,
      specularSharpness: GlassSpecularSharpness.soft,
      standardOpacityMultiplier: 1.0,
      shadowElevation: 0.0,
      shadow: null,
      whitenStrength: 0.0,
      whitenGated: false,
      backerColor: CupertinoColors.transparent,
      platformViewFallbackColor: CupertinoColors.transparent,
    );
  }

  const App({super.key});

  @override
  State<App> createState() => _AppState();
}

class _AppState extends State<App> with WidgetsBindingObserver {
  bool _blueDisposed = false;

  @override
  void initState() {
    super.initState();
    WidgetsBinding.instance.addObserver(this);
  }

  @override
  void dispose() {
    WidgetsBinding.instance.removeObserver(this);
    _disposeBlueResourceIfNeeded();
    super.dispose();
  }

  @override
  void didChangeAppLifecycleState(AppLifecycleState state) {
    // The app enters detached before exiting; release Bluetooth resources here.
    if (state == AppLifecycleState.detached) {
      _disposeBlueResourceIfNeeded();
    }
  }

  void _disposeBlueResourceIfNeeded() {
    if (_blueDisposed) return;
    _blueDisposed = true;
    BlueUtil.shared.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return CupertinoApp(
      navigatorKey: App.navigatorKey,
      home: const Home(),
      builder: (context, child) {
        return AppSystemUiOverlay(
          child: UnfocusOnTapOutside(
            child: GlobalToast(
              // iOS 26 pinned toolbar: on iPhone Duo, the TabBar moves into a vertical strip at the side.
              child: AdaptiveToolbarHost(
                child: child ?? const SizedBox.shrink(),
              ),
            ),
          ),
        );
      },
      debugShowCheckedModeBanner: false,
      theme: CupertinoThemeData(
        primaryColor: CupertinoColors.systemOrange.resolveFrom(context),
      ),
      title: "StackChan World",
      localizationsDelegates: const [
        DefaultMaterialLocalizations.delegate,
        DefaultCupertinoLocalizations.delegate,
        DefaultWidgetsLocalizations.delegate,
      ],
      navigatorObservers: [App.routeObserver],
      locale: const Locale("en", "US"),
    );
  }
}

class AppSystemUiOverlay extends StatelessWidget {
  const AppSystemUiOverlay({required this.child, super.key});

  final Widget child;

  @override
  Widget build(BuildContext context) {
    final appBrightness = CupertinoTheme.brightnessOf(context);
    final style = SystemUiOverlayStyle(
      statusBarColor: CupertinoColors.transparent,
      systemNavigationBarColor: CupertinoColors.transparent,
      systemNavigationBarDividerColor: CupertinoColors.transparent,
      systemStatusBarContrastEnforced: false,
      systemNavigationBarContrastEnforced: false,
      systemNavigationBarIconBrightness: appBrightness == Brightness.dark
          ? Brightness.light
          : Brightness.dark,
    );
    return AnnotatedRegion<SystemUiOverlayStyle>(value: style, child: child);
  }
}
