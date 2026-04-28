/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

import 'package:flutter/cupertino.dart';
import 'package:stack_chan/app_state.dart';

import '../util/gradation_page_background.dart';

class Nearby extends StatefulWidget {
  const Nearby({super.key});

  @override
  State<StatefulWidget> createState() => _NearbyState();
}

class _NearbyState extends State<Nearby> {

  @override
  void initState() {
    super.initState();
    ///startGetLocation info
    AppState.shared.obtainLocation();
  }

  @override
  Widget build(BuildContext context) {
    return CupertinoPageScaffold(
      child: GradationPageBackground(child: SizedBox.expand()),
    );
  }
}
