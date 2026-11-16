/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

import 'package:cupertino_ui/cupertino_ui.dart';
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

    /// Starts fetching the device location.
    AppState.shared.obtainLocation();
  }

  @override
  Widget build(BuildContext context) {
    return CupertinoPageScaffold(
      backgroundColor: CupertinoColors.systemGroupedBackground.resolveFrom(
        context,
      ),
      child: const GradationPageBackground(child: SizedBox.expand()),
    );
  }
}
