/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

import 'package:flutter/services.dart';

/// Created 2024-01-08.
/// Status bar styling helpers.
/// Utilities for configuring the status bar and navigation bar.

class StatusBarManagement {
  static StatusBarManagement? _statusBarManagement;

  static StatusBarManagement getInstance() {
    return _statusBarManagement ?? StatusBarManagement();
  }

  /// Configure an immersive status bar and its icon brightness.
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

  /// Set the status bar color and icon appearance.
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

  /// Set the status bar and navigation bar colors and icon brightness.
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

  /// Configure immersive status bar and navigation bar icon brightness.
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

  /// Set status bar and navigation bar colors, with optional immersive mode.
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
