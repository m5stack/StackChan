/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

import 'package:flutter/cupertino.dart';
import 'package:get/get_core/src/get_main.dart';
import 'package:get/get_instance/src/extension_instance.dart';
import 'package:stack_chan/util/audio_engine_manager.dart';
import 'package:stack_chan/view/app.dart';

import 'app_state.dart';

void main() async {
  WidgetsFlutterBinding.ensureInitialized();
  Get.put(AppState());
  await AppState.shared.initData();
  AudioEngineManager.shared.init();
  runApp(App());
}
