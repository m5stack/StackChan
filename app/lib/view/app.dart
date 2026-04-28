/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart';
import 'package:stack_chan/view/popup/cupertino_popup_windows.dart';

import '../util/app_toast.dart';
import 'home/home.dart';

class App extends StatelessWidget {
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
              child: Text("Confirm"),
              onPressed: () {
                Navigator.of(context).pop();
              },
            ),
          ],
        );
      },
    );
  }

  static void showAppModalPopup(
    WidgetBuilder builder, {
    bool barrierDismissible = true,
    bool useRootNavigator = true,
    SheetSize sheetSize = .medium,
  }) {
    showCupertinoSheet(
      context: appContext(),
      builder: builder,
      useNestedNavigation: useRootNavigator,
    );
    // showCupertinoPopupWindows(
    //   context: appContext(),
    //   builder: builder,
    //   useRootNavigator: useRootNavigator,
    //   barrierDismissible: barrierDismissible,
    //   sheetSize: sheetSize,
    // );
  }

  static Future<T?> showAppSheet<T>(
    WidgetBuilder builder, {
    bool useNestedNavigation = false,
    bool enableDrag = true,
    bool showDragHandle = false,
  }) async {
    return showCupertinoSheet(
      context: appContext(),
      builder: builder,
      useNestedNavigation: useNestedNavigation,
      enableDrag: enableDrag,
      showDragHandle: showDragHandle,
    );
  }

  static final RouteObserver<PageRoute> routeObserver =
      RouteObserver<PageRoute>();

  const App({super.key});

  @override
  Widget build(BuildContext context) {
    return CupertinoApp(
      navigatorKey: navigatorKey,
      home: const Home(),
      builder: (context, child) {
        return AppToast(child: child);
      },
      debugShowCheckedModeBanner: false,
      theme: CupertinoThemeData(
        primaryColor: CupertinoColors.systemOrange.resolveFrom(context),
      ),
      localizationsDelegates: const [
        DefaultMaterialLocalizations.delegate,
        DefaultCupertinoLocalizations.delegate,
        DefaultWidgetsLocalizations.delegate,
      ],
      navigatorObservers: [routeObserver],
    );
  }
}
