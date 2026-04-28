/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

import 'dart:math';

import 'package:flutter/cupertino.dart';
import 'package:liquid_glass_renderer/liquid_glass_renderer.dart';

import '../popup/cupertino_popup_windows.dart';

class CupertinoSheet extends StatelessWidget {
  const CupertinoSheet({
    super.key,
    required this.builder,
    this.backgroundColor = CupertinoColors.transparent,
    this.sheetSize = .medium,
  });

  final WidgetBuilder builder;
  final Color backgroundColor;
  final SheetSize sheetSize;

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: MediaQuery.of(context).padding,
      child: LayoutBuilder(
        builder: (context, constraints) {
          double height;
          switch (sheetSize) {
            case SheetSize.medium:
              height = constraints.maxHeight / 2 * 1;
            case SheetSize.large:
              height = constraints.maxHeight;
          }
          return SizedBox(
            height: height,
            child: Padding(
              padding: .all(10),
              child: LiquidGlassLayer(
                settings: LiquidGlassSettings(lightAngle: 0.25 * pi),
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
  }
}
