/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

import 'package:flutter/services.dart';

///createtime：2024/1/8
///Author:袁智鸿
///Description:status barManagertoolClass

class StatusBarManagement {
  static StatusBarManagement? _statusBarManagement;

  static StatusBarManagement getInstance() {
    return _statusBarManagement ?? StatusBarManagement();
  }

  ///set沉浸式status barfontandicon
  void setStatusBarImmerse(Brightness statusBarBrightness) {
    SystemUiOverlayStyle style = SystemUiOverlayStyle(
      statusBarColor: const Color(0x00000000),
      statusBarIconBrightness: statusBarBrightness,
    );
    SystemChrome.setSystemUIOverlayStyle(style);
    SystemChrome.setEnabledSystemUIMode(
      SystemUiMode.edgeToEdge,
      overlays: [SystemUiOverlay.top],
    );
  }

  ///setstatus barcolorfontandicon
  void setStatusColor(
    Color color,
    Brightness statusBarBrightness, {
    Color? navigationColor,
    Brightness? navigationBrightness,
  }) {
    Brightness status;
    if (statusBarBrightness == Brightness.light) {
      status = Brightness.dark;
    } else {
      status = Brightness.light;
    }
    Brightness navigation;
    if (navigationBrightness == Brightness.light) {
      navigation = Brightness.dark;
    } else {
      navigation = Brightness.light;
    }

    SystemUiOverlayStyle style = SystemUiOverlayStyle(
      statusBarColor: color,
      systemNavigationBarColor: navigationColor,
      systemNavigationBarIconBrightness: navigation,
      statusBarIconBrightness: status,
      statusBarBrightness: Brightness.light,
    );
    SystemChrome.setSystemUIOverlayStyle(style);
    SystemChrome.setEnabledSystemUIMode(
      SystemUiMode.manual,
      overlays: [SystemUiOverlay.top, SystemUiOverlay.bottom],
    );
  }

  void setBrightness(
    Brightness statusBarBrightness,
    Brightness navigationBrightness,
  ) {
    Brightness status;
    if (statusBarBrightness == Brightness.light) {
      status = Brightness.dark;
    } else {
      status = Brightness.light;
    }
    Brightness navigation;
    if (navigationBrightness == Brightness.light) {
      navigation = Brightness.dark;
    } else {
      navigation = Brightness.light;
    }
    SystemUiOverlayStyle style = SystemUiOverlayStyle(
      statusBarIconBrightness: status,
      systemNavigationBarIconBrightness: navigation,
    );
    SystemChrome.setSystemUIOverlayStyle(style);
  }

  ///setstatus barandnavigation barfontandicon
  void setStatusBarAndNavigationBarDark(
    Color statusBarColor,
    Color navigationColor,
    Brightness statusBarBrightness,
    Brightness navigationBrightness,
  ) {
    Brightness status;
    if (statusBarBrightness == Brightness.light) {
      status = Brightness.dark;
    } else {
      status = Brightness.light;
    }
    Brightness navigation;
    if (navigationBrightness == Brightness.light) {
      navigation = Brightness.dark;
    } else {
      navigation = Brightness.light;
    }
    SystemUiOverlayStyle style = SystemUiOverlayStyle(
      statusBarColor: statusBarColor,
      systemStatusBarContrastEnforced: false,
      statusBarIconBrightness: status,
      statusBarBrightness: Brightness.light,
      systemNavigationBarColor: navigationColor,
      systemNavigationBarIconBrightness: navigation,
      systemNavigationBarContrastEnforced: false,
    );
    SystemChrome.setSystemUIOverlayStyle(style);
    SystemChrome.setEnabledSystemUIMode(
      SystemUiMode.manual,
      overlays: [SystemUiOverlay.top, SystemUiOverlay.bottom],
    );
  }

  ///set沉浸式status barandnavigation barfontandicon
  void setStatusBarAndNavigationBarImmerseDark(
    Brightness statusBarBrightness,
    Brightness navigationBrightness,
  ) {
    Brightness status;
    if (statusBarBrightness == Brightness.light) {
      status = Brightness.dark;
    } else {
      status = Brightness.light;
    }
    Brightness navigation;
    if (navigationBrightness == Brightness.light) {
      navigation = Brightness.dark;
    } else {
      navigation = Brightness.light;
    }

    SystemUiOverlayStyle style = SystemUiOverlayStyle(
      statusBarColor: const Color(0x00000000),
      systemNavigationBarColor: const Color(0x00000000),
      systemNavigationBarIconBrightness: navigation,
      systemNavigationBarContrastEnforced: false,
      statusBarIconBrightness: status,
      statusBarBrightness: Brightness.light,
      systemStatusBarContrastEnforced: false,
    );
    SystemChrome.setSystemUIOverlayStyle(style);
    SystemChrome.setEnabledSystemUIMode(
      SystemUiMode.edgeToEdge,
      overlays: [SystemUiOverlay.top, SystemUiOverlay.bottom],
    );
  }

  ///setstatus barandnavigation barcustom
  void setStatusBarAndNavigationBarCustom({
    bool isImmerse = false,
    Color? statusBackColor,
    Color? navigationBackColor,
    bool statusIsLight = false,
  }) {
    Brightness statusBarIconBrightness = Brightness.dark;
    Brightness statusBarBrightness = Brightness.light;
    if (statusIsLight) {
      statusBarIconBrightness = Brightness.light;
    } else {
      statusBarIconBrightness = Brightness.dark;
    }
    if (statusIsLight) {
      statusBarBrightness = Brightness.dark;
    } else {
      statusBarBrightness = Brightness.light;
    }
    SystemUiOverlayStyle style = SystemUiOverlayStyle(
      systemNavigationBarColor: navigationBackColor ?? const Color(0x00000000),
      systemNavigationBarContrastEnforced: false,
      systemStatusBarContrastEnforced: false,
      statusBarColor: statusBackColor ?? const Color(0x00000000),
      statusBarIconBrightness: statusBarIconBrightness,
      statusBarBrightness: statusBarBrightness,
    );
    SystemChrome.setSystemUIOverlayStyle(style);
    if (isImmerse) {
      SystemChrome.setEnabledSystemUIMode(
        SystemUiMode.edgeToEdge,
        overlays: [SystemUiOverlay.top, SystemUiOverlay.bottom],
      );
    } else {
      SystemChrome.setEnabledSystemUIMode(
        SystemUiMode.manual,
        overlays: [SystemUiOverlay.top, SystemUiOverlay.bottom],
      );
    }
  }
}
