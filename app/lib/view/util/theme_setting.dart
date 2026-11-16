/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

import 'package:cupertino_ui/cupertino_ui.dart';

class ThemeSetting {
  static const double cardRadius = 12;

  static const double mediumCardRadius = 40;

  static const double horizontalScreenMarginRatio = 7;

  static const double navigationBarButtonTextPadding = 5;

  static BoxDecoration dialogTextFieldDecoration(BuildContext context) {
    return BoxDecoration(
      color: CupertinoColors.tertiarySystemFill.resolveFrom(context),
      borderRadius: .circular(cardRadius),
    );
  }

  static Color cupertinoNavigationBarBackgroundColor(BuildContext context) {
    final brightness = CupertinoTheme.brightnessOf(context);
    if (brightness == .light) {
      return CupertinoColors.white.withValues(alpha: 0.8);
    } else {
      return CupertinoColors.systemGrey5
          .resolveFrom(context)
          .withValues(alpha: 0.7);
    }
  }

  static Border cupertinoNavigationBarBorder(BuildContext context) {
    return Border(
      bottom: BorderSide(
        color: CupertinoColors.separator.resolveFrom(context),
        width: 1.0 / MediaQuery.devicePixelRatioOf(context),
      ),
    );
  }

  static const EdgeInsetsDirectional navigationBarPadding = .only(
    start: 8,
    end: 8,
  );

  static EdgeInsets screenPadding(BuildContext context) {
    final mediaQueryPadding = MediaQuery.paddingOf(context);
    final orientation = MediaQuery.orientationOf(context);
    final size = MediaQuery.sizeOf(context);
    double leftPadding = mediaQueryPadding.left;
    double rightPadding = mediaQueryPadding.right;
    if (orientation == .landscape) {
      leftPadding =
          (size.width / ThemeSetting.horizontalScreenMarginRatio) + leftPadding;
      rightPadding =
          (size.width / ThemeSetting.horizontalScreenMarginRatio) +
          rightPadding;
    }
    return .only(
      left: leftPadding,
      right: rightPadding,
      top: mediaQueryPadding.top,
      bottom: mediaQueryPadding.bottom,
    );
  }
}
