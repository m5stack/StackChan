/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

import 'package:flutter/cupertino.dart';

import '../util/gradation_page_background.dart';

class Moments extends StatefulWidget {
  const Moments({super.key});

  @override
  State<StatefulWidget> createState() => _MomentsState();
}

class _MomentsState extends State<Moments> {
  @override
  Widget build(BuildContext context) {
    return CupertinoPageScaffold(
      child: GradationPageBackground(child: SizedBox.expand()),
    );
  }
}
