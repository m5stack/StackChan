/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

import 'package:flutter/cupertino.dart';

class GradationPageBackground extends StatelessWidget {
  final Widget? child;

  const GradationPageBackground({super.key, this.child});

  @override
  Widget build(BuildContext context) {
    return Container(
      width: .infinity,
      height: .infinity,
      decoration: BoxDecoration(
        gradient: LinearGradient(
          begin: Alignment.topLeft,
          end: Alignment.bottomRight,
          colors: [
            CupertinoColors.activeBlue.withValues(alpha: 0.5),
            CupertinoColors.systemPink.withValues(alpha: 0.1),
            CupertinoColors.systemBlue.withValues(alpha: 0.2),
          ],
        ),
      ),
      child: child,
    );
  }
}
