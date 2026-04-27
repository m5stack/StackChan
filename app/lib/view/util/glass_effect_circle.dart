/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

import 'package:flutter/cupertino.dart';

///圆形
class GlassEffectCircle extends StatelessWidget {
  const GlassEffectCircle({super.key, this.padding, this.child});

  final EdgeInsetsGeometry? padding;
  final Widget? child;

  @override
  Widget build(BuildContext context) {
    return ClipOval(
      clipBehavior: .antiAliasWithSaveLayer,
      child: BackdropFilter(
        filter: .blur(sigmaX: 10, sigmaY: 10),
        child: Padding(padding: padding ?? .zero, child: child),
      ),
    );
  }
}

// class GlassEffectRegular extends StatelessWidget {
//   const GlassEffectRegular({super.key, this.padding, this.borderRadius});
//
//   final EdgeInsetsGeometry? padding;
//   final BorderRadius? borderRadius;
//
//   @override
//   Widget build(BuildContext context) {
//     // TODO: implement build
//     throw UnimplementedError();
//   }
// }
