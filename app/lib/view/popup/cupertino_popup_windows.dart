/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

import 'dart:math';

import 'package:flutter/cupertino.dart';
import 'package:liquid_glass_renderer/liquid_glass_renderer.dart';

enum SheetSize { medium, large }

Widget _defaultTransitionsBuilder(
  BuildContext context,
  Animation<double> animation,
  Animation<double> secondaryAnimation,
  Widget child,
) {
  return child;
}

Animation<Offset> _getSlideAnimation(
  Alignment alignment,
  Animation<double> animation,
) {
  final curvedAnimation = CurvedAnimation(
    parent: animation,
    curve: Curves.fastOutSlowIn,
  );
  return Tween<Offset>(
    begin: const Offset(0, 1),
    end: Offset.zero,
  ).animate(curvedAnimation);
}

Widget _buildBody(
  BuildContext context,
  WidgetBuilder builder,
  Animation<double> animation,
  SheetSize sheetSize,
) {
  final body = Padding(
    padding: MediaQuery.of(context).padding,
    child: LayoutBuilder(
      builder: (context, constraints) {
        double height;
        switch (sheetSize) {
          case SheetSize.medium:
            height = constraints.maxHeight / 2;
          case SheetSize.large:
            height = constraints.maxHeight;
        }
        return SizedBox(
          height: height,
          child: Padding(
            padding: .all(10),
            child: LiquidGlassLayer(
              settings: LiquidGlassSettings(
                lightAngle: 0.25 * pi,
                refractiveIndex: 1.5,
              ),
              child: LiquidStretch(
                child: LiquidGlass(
                  shape: LiquidRoundedSuperellipse(borderRadius: 25),
                  child: GlassGlow(child: builder(context)),
                ),
              ),
            ),
          ),
        );
      },
    ),
  );

  final slideAnimation = _getSlideAnimation(.bottomCenter, animation);
  return SlideTransition(position: slideAnimation, child: body);
}

Future<T?> showCupertinoPopupWindows<T>({
  required BuildContext context,
  required WidgetBuilder builder,
  bool useRootNavigator = true,
  Duration transitionDuration = const Duration(milliseconds: 400),
  Duration reverseTransitionDuration = const Duration(milliseconds: 400),
  RouteTransitionsBuilder transitionsBuilder = _defaultTransitionsBuilder,
  bool opaque = false,
  bool barrierDismissible = true,
  Color? barrierColor,
  String barrierLabel = 'Dismiss',
  bool maintainState = true,
  bool allowSnapshotting = true,
  SheetSize sheetSize = .medium,
}) {
  final GlobalKey<NavigatorState> nestedNavigatorKey =
      GlobalKey<NavigatorState>();

  Widget widgetBuilder(BuildContext context) {
    return NavigatorPopHandler(
      onPopWithResult: (T? result) {
        nestedNavigatorKey.currentState!.maybePop();
      },
      child: Navigator(
        key: nestedNavigatorKey,
        initialRoute: '/',
        onGenerateInitialRoutes:
            (NavigatorState navigator, String initialRouteName) {
              return <Route<void>>[
                CupertinoPageRoute<void>(
                  builder: (BuildContext context) {
                    return PopScope(
                      canPop: false,
                      onPopInvokedWithResult: (bool didPop, Object? result) {
                        if (didPop) {
                          return;
                        }
                        Navigator.of(context, rootNavigator: true).pop(result);
                      },
                      child: builder(context),
                    );
                  },
                ),
              ];
            },
      ),
    );
  }

  return Navigator.of(context, rootNavigator: useRootNavigator).push<T>(
    CupertinoPopupRoute(
      builder: widgetBuilder,
      sheetSize: sheetSize,
      transitionDuration: transitionDuration,
      reverseTransitionDuration: reverseTransitionDuration,
      transitionsBuilder: transitionsBuilder,
      barrierDismissible: barrierDismissible,
      barrierColor: barrierColor,
      barrierLabel: barrierLabel,
      maintainState: maintainState,
      allowSnapshotting: allowSnapshotting,
      opaque: opaque,
    ),
  );
}

class CupertinoPopupRoute<T> extends PopupRoute<T> {
  final WidgetBuilder builder;
  final RouteTransitionsBuilder transitionsBuilder;
  final SheetSize sheetSize;

  CupertinoPopupRoute({
    required this.builder,
    required this.sheetSize,
    this.transitionsBuilder = _defaultTransitionsBuilder,
    this.transitionDuration = const Duration(milliseconds: 400),
    this.reverseTransitionDuration = const Duration(milliseconds: 400),
    this.barrierLabel,
    this.barrierDismissible = true,
    this.barrierColor,
    this.maintainState = true,
    this.allowSnapshotting = true,
    this.opaque = false,
  });

  @override
  bool opaque;

  @override
  bool allowSnapshotting;

  @override
  bool maintainState;

  @override
  final Duration transitionDuration;

  @override
  Duration reverseTransitionDuration;

  @override
  Color? barrierColor;

  @override
  bool barrierDismissible;

  @override
  String? barrierLabel;

  @override
  Widget buildPage(
    BuildContext context,
    Animation<double> animation,
    Animation<double> secondaryAnimation,
  ) {
    return AnimatedBuilder(
      animation: animation,
      builder: (context, child) {
        return Container(
          color: CupertinoColors.systemFill
              .resolveFrom(context)
              .withValues(alpha: 0.2),
          alignment: .bottomCenter,
          child: _buildBody(context, builder, animation, sheetSize),
        );
      },
    );
  }

  @override
  Widget buildTransitions(
    BuildContext context,
    Animation<double> animation,
    Animation<double> secondaryAnimation,
    Widget child,
  ) {
    return transitionsBuilder(context, animation, secondaryAnimation, child);
  }
}
