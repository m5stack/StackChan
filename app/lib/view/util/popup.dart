/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

import 'dart:io';

import 'package:cupertino_ui/cupertino_ui.dart';

/// Shows a popup as a sheet on compact screens and as a centered dialog on
/// wide screens and desktop platforms.
///
/// Close it with [closeCustomCupertinoSheet] rather than
/// [CupertinoSheetRoute.popSheet]: the latter only understands the sheet
/// presentation and silently does nothing for the dialog presentation.
Future<T?> showCustomCupertinoSheet<T>({
  required BuildContext context,
  required ScrollableWidgetBuilder scrollableBuilder,
  bool useNestedNavigation = false,
  bool enableDrag = false,
  bool showDragHandle = false,
  bool barrierDismissible = false,
}) {
  final size = MediaQuery.sizeOf(context);
  final orientation = MediaQuery.orientationOf(context);
  final ratio = size.shortestSide / size.longestSide;
  final GlobalKey<NavigatorState>? nestedNavigatorKey = useNestedNavigation
      ? GlobalKey<NavigatorState>()
      : null;

  double targetValue = 0.62;

  if (ratio > targetValue && size.longestSide > 800) {
    double width = 0;
    double height = 0;
    if (orientation == .landscape) {
      width = size.width / 2;
      height = size.height / 8 * 7;
    } else {
      width = size.width / 4 * 3;
      height = size.height / 9 * 5;
    }
    return Navigator.of(context, rootNavigator: true).push<T>(
      CupertinoDialogRoute<T>(
        context: context,
        requestFocus: false,
        barrierDismissible: barrierDismissible,
        barrierColor: CupertinoColors.systemFill.withValues(alpha: 0.3),
        transitionBuilder: _buildIpadCenteredDialogTransition,
        builder: (dialogContext) {
          final Route<dynamic>? route = ModalRoute.of(dialogContext);
          final ScrollController scrollController = ScrollController();

          Widget buildContent(BuildContext contentContext) {
            return scrollableBuilder(contentContext, scrollController);
          }

          final Widget content = MediaQuery.removePadding(
            context: dialogContext,
            removeTop: true,
            removeBottom: true,
            removeLeft: true,
            removeRight: true,
            child: useNestedNavigation
                ? _nestedNavigator(nestedNavigatorKey!, buildContent)
                : buildContent(dialogContext),
          );

          return Center(
            child: Container(
              width: width,
              height: height,
              clipBehavior: .antiAliasWithSaveLayer,
              decoration: ShapeDecoration(
                shape: RoundedSuperellipseBorder(borderRadius: .circular(12)),
              ),
              child: route == null
                  ? content
                  : _CustomPopupScope(route: route, child: content),
            ),
          );
        },
      ),
    );
  } else {
    return showCupertinoSheet<T>(
      context: context,
      scrollableBuilder: (sheetContext, scrollController) {
        final Route<dynamic>? route = ModalRoute.of(sheetContext);

        Widget buildContent(BuildContext contentContext) {
          return ClipRSuperellipse(
            clipBehavior: .antiAliasWithSaveLayer,
            borderRadius: const .only(
              topRight: .circular(12),
              topLeft: .circular(12),
            ),
            child: scrollableBuilder(contentContext, scrollController),
          );
        }

        final Widget content = useNestedNavigation
            ? _nestedNavigator(nestedNavigatorKey!, buildContent)
            : buildContent(sheetContext);

        return route == null
            ? content
            : _CustomPopupScope(route: route, child: content);
      },
      useNestedNavigation: false,
      enableDrag: enableDrag,
      showDragHandle: showDragHandle,
      topGap: orientation == .landscape ? (Platform.isIOS ? 0 : 0.1) : null,
    );
    // if (orientation == .landscape) {
    //   final screenPadding = ThemeSetting.screenPadding(context);
    //   return showCupertinoSheet<T>(
    //     context: context,
    //     scrollableBuilder: (context, scrollController) {
    //       return MediaQuery.removePadding(
    //         context: context,
    //         removeRight: true,
    //         removeLeft: true,
    //         child: Padding(
    //           padding: .only(
    //             left: screenPadding.left,
    //             right: screenPadding.right,
    //           ),
    //           child: scrollableBuilder(context, scrollController),
    //         ),
    //       );
    //     },
    //     useNestedNavigation: useNestedNavigation,
    //     enableDrag: enableDrag,
    //     showDragHandle: showDragHandle,
    //     topGap: Platform.isIOS ? 0 : 0.1,
    //   );
    // } else {
    //   return showCupertinoSheet<T>(
    //     context: context,
    //     scrollableBuilder: (context, scrollController) {
    //       return ClipRSuperellipse(
    //         clipBehavior: .antiAliasWithSaveLayer,
    //         borderRadius: const .only(
    //           topRight: .circular(12),
    //           topLeft: .circular(12),
    //         ),
    //         child: scrollableBuilder(context, scrollController),
    //       );
    //     },
    //     useNestedNavigation: useNestedNavigation,
    //     enableDrag: enableDrag,
    //     showDragHandle: showDragHandle,
    //     topGap: null,
    //   );
    // }
  }
}

/// Closes the popup opened by [showCustomCupertinoSheet].
///
/// [showCustomCupertinoSheet] presents its content as a [CupertinoSheetRoute]
/// on compact screens and as a [CupertinoDialogRoute] on wide screens and
/// desktop platforms. [CupertinoSheetRoute.popSheet] only pops when a sheet is
/// an ancestor of [context], so it silently does nothing for the dialog
/// presentation (and for pages pushed above a sheet).
///
/// This helper handles all presentations:
///  * when [context] is inside a sheet or dialog, that route is popped;
///  * when [context] is inside a page pushed into a popup's nested navigator,
///    the whole sheet/dialog is popped, matching
///    [CupertinoSheetRoute.popSheet];
///  * otherwise the enclosing page is popped, so closing never silently no-ops.
///
/// Returns `true` when a route was popped.
bool closeCustomCupertinoSheet(BuildContext context) {
  final ModalRoute<dynamic>? route = ModalRoute.of(context);

  // Directly inside a sheet or a dialog opened by showCustomCupertinoSheet.
  if (route != null &&
      (route is CupertinoSheetRoute<dynamic> || route is PopupRoute)) {
    if (_popRoute(route)) {
      return true;
    }
  }

  // A page pushed inside the popup's nested Navigator. Close the popup itself,
  // like CupertinoSheetRoute.popSheet would.
  final _CustomPopupScope? scope = _CustomPopupScope.maybeOf(context);
  if (scope != null && _popRoute(scope.route)) {
    return true;
  }

  // A sheet created outside showCustomCupertinoSheet.
  if (CupertinoSheetRoute.hasParentSheet(context)) {
    return _popNavigator(Navigator.of(context, rootNavigator: true));
  }

  // A plain page, or content pushed above a sheet. Pop it instead of silently
  // doing nothing.
  return _popNavigator(Navigator.of(context));
}

bool _popRoute(Route<dynamic> route) {
  final NavigatorState? navigator = route.navigator;
  if (navigator == null || !route.isActive) {
    return false;
  }
  // Pop anything currently shown above the target route first (for example an
  // alert dialog), then pop the target route itself.
  navigator.popUntil((Route<dynamic> candidate) => identical(candidate, route));
  navigator.pop();
  return true;
}

bool _popNavigator(NavigatorState navigator) {
  if (!navigator.canPop()) {
    return false;
  }
  navigator.pop();
  return true;
}

/// Builds a Navigator inside the popup so that routes pushed by the popup's
/// content stay inside the sheet/dialog instead of covering the whole app.
Widget _nestedNavigator(
  GlobalKey<NavigatorState> navigatorKey,
  WidgetBuilder builder,
) {
  return NavigatorPopHandler<Object?>(
    onPopWithResult: (Object? result) {
      navigatorKey.currentState?.maybePop();
    },
    child: Navigator(
      key: navigatorKey,
      initialRoute: '/',
      onGenerateInitialRoutes:
          (NavigatorState navigator, String initialRouteName) {
            return <Route<void>>[
              CupertinoPageRoute<void>(
                builder: (BuildContext context) {
                  return PopScope<Object?>(
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

/// Makes the popup's route available to [closeCustomCupertinoSheet] from
/// inside nested routes.
class _CustomPopupScope extends InheritedWidget {
  const _CustomPopupScope({required this.route, required super.child});

  final Route<dynamic> route;

  static _CustomPopupScope? maybeOf(BuildContext context) {
    return context.getInheritedWidgetOfExactType<_CustomPopupScope>();
  }

  @override
  bool updateShouldNotify(_CustomPopupScope oldWidget) => false;
}

/// Animates the wide-screen popup like an iPad centered form sheet instead of
/// the stock iPhone alert transition.
///
/// [CupertinoDialogRoute]'s default transition scales the dialog from 1.3,
/// which is too aggressive for a large centered card. iPad form sheets lift up
/// slightly while fading in, with only a small amount of scale.
Widget _buildIpadCenteredDialogTransition(
  BuildContext context,
  Animation<double> animation,
  Animation<double> secondaryAnimation,
  Widget child,
) {
  // Fade in quickly; fade out over the whole reverse transition so the card
  // does not linger while it shrinks.
  final Animation<double> fade = animation.status == AnimationStatus.reverse
      ? animation
      : animation.drive(
          CurveTween(curve: const Interval(0.0, 0.7, curve: Curves.easeOut)),
        );

  final Animation<double> scale = animation.drive(
    Tween<double>(begin: 0.94, end: 1.0),
  );

  final Animation<Offset> slide = animation.drive(
    Tween<Offset>(begin: const Offset(0, 0.025), end: Offset.zero),
  );

  return FadeTransition(
    opacity: fade,
    child: SlideTransition(
      position: slide,
      child: ScaleTransition(scale: scale, child: child),
    ),
  );
}
