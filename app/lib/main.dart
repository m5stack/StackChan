/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

import 'package:cupertino_ui/cupertino_ui.dart';
import 'package:get/get_core/src/get_main.dart';
import 'package:get/get_instance/src/extension_instance.dart';
import 'package:liquid_glass_widgets/liquid_glass_widgets.dart';
// Audio support is temporarily disabled; re-enable this import once it is fully implemented.
// import 'package:stack_chan/util/audio_engine_manager.dart';
import 'package:stack_chan/util/debug_log_buffer.dart';
import 'package:stack_chan/view/app.dart';

import 'app_state.dart';

void main() async {
  WidgetsFlutterBinding.ensureInitialized();
  DebugLogBuffer.install();
  await LiquidGlassWidgets.initialize();
  Get.put(AppState());
  await AppState.shared.initData();
  // Audio support is temporarily disabled; restore this initialization once it is fully implemented.
  // AudioEngineManager.shared.init();
  runApp(LiquidGlassWidgets.wrap(child: const App()));
}

// Audio debug helpers are temporarily disabled; restore them once audio support is fully implemented.
// void _debugTapForAudioTest(Offset position) {
//   final binding = WidgetsFlutterBinding.ensureInitialized();
//   binding.handlePointerEvent(PointerDownEvent(position: position));
//   binding.handlePointerEvent(PointerUpEvent(position: position));
// }
//
// @pragma('vm:entry-point')
// void debugTapAvatarForAudioTest() {
//   _debugTapForAudioTest(const Offset(207, 401));
// }
//
// @pragma('vm:entry-point')
// void debugTapCallForAudioTest() {
//   _debugTapForAudioTest(const Offset(256, 805));
// }
