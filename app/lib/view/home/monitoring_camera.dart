/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

import 'dart:async';

import 'package:cupertino_ui/cupertino_ui.dart';
import 'package:stack_chan/view/util/theme_setting.dart';
import 'package:flutter/services.dart';
import 'package:get/get.dart';
import 'package:stack_chan/app_state.dart';
import 'package:stack_chan/model/expression_data.dart';
import 'package:stack_chan/model/msg_type.dart';
import 'package:stack_chan/network/local_area_network_service.dart';
import 'package:stack_chan/network/web_socket_util.dart';
import 'package:stack_chan/util/audio_engine_manager.dart';
import 'package:stack_chan/util/extension.dart';

import '../../util/value_constant.dart';
import '../util/grid_coordinate_joystick.dart';

class MonitoringCamera extends StatefulWidget {
  const MonitoringCamera({super.key});

  @override
  State<StatefulWidget> createState() => _MonitoringCameraState();
}

class _MonitoringCameraState extends State<MonitoringCamera> {
  late final String tag = "MonitoringCamera-${identityHashCode(this)}";

  MotionData motionData = MotionData(
    pitchServo: MotionDataItem(),
    yawServo: MotionDataItem(),
  );

  DateTime sendScreenLastTime = DateTime.now();
  DateTime lastJpegReceivedTime = DateTime.now();
  Timer? cameraMonitorTimer;
  late final String deviceMac;
  StreamSubscription<LocalAreaNetworkMessage>? _localMessages;
  StreamSubscription<bool>? _localConnections;

  Rx<Uint8List> cameraImage = Rx(Uint8List(0));
  RxBool onMic = RxBool(false);

  @override
  void initState() {
    super.initState();
    deviceMac = AppState.shared.deviceMac;
    final lan = LocalAreaNetworkService.instance;
    // Hold the LAN service while the CAMERA page is open.
    unawaited(lan.retainForPage());
    _localMessages = lan.messages.listen((message) {
      if (lan.canSendTo(deviceMac)) {
        _handleMessage(message.type, message.payload);
      }
    });
    _localConnections = lan.connectionStates.listen((connected) {
      if (!connected || lan.canSendTo(deviceMac)) _sendInitialCommands();
    });
    initCameraAndSocket();
    startCameraMonitor();
    WebSocketUtil.shared.addConnectionObserver(this, () {
      if (!lan.canSendTo(deviceMac)) _sendInitialCommands();
    });
    initPromptWindow();
  }

  Future<void> initPromptWindow() async {
    bool? isPromptWindowVisible = await AppState.asyncPrefs.getBool(
      ValueConstant.monitoringCameraPromptWindow,
    );
    if (isPromptWindowVisible == null || isPromptWindowVisible == false) {
      await AppState.asyncPrefs.setBool(
        ValueConstant.monitoringCameraPromptWindow,
        true,
      );
      if (mounted) {
        AppState.shared.showSwitchDeviceAvatar(context);
      }
    }
  }

  @override
  void dispose() {
    _cleanResources();
    // Release the LAN service on exit; advertising stops once no page holds it.
    unawaited(LocalAreaNetworkService.instance.releaseForPage());
    super.dispose();
  }

  void startCameraMonitor() {
    cameraMonitorTimer?.cancel();
    cameraMonitorTimer = Timer.periodic(const Duration(seconds: 3), (_) {
      if (!mounted || deviceMac.isEmpty) return;
      final now = DateTime.now();
      final diffSeconds = now.difference(lastJpegReceivedTime).inSeconds;
      if (diffSeconds >= 3) {
        _send(.onCamera, data: deviceMac.toUint8List());
      }
    });
  }

  //====================== core initialization ======================
  Future<void> initCameraAndSocket() async {
    WebSocketUtil.shared.removeObserver(tag);
    WebSocketUtil.shared.addObserver(tag, (message) {
      if (!LocalAreaNetworkService.instance.canSendTo(deviceMac) &&
          message is Uint8List) {
        final result = AppState.shared.parseMessage(message);
        final msgType = result.$1;
        final parsedData = result.$2;
        _handleMessage(msgType, parsedData);
      }
    });

    _sendInitialCommands();
  }

  void _handleMessage(MsgType? type, Uint8List? data) {
    if (!mounted || data == null) return;
    switch (type) {
      case MsgType.jpeg:
        lastJpegReceivedTime = DateTime.now();
        cameraImage.value = data;
        break;
      case MsgType.opus:
        AudioEngineManager.shared.playOpus(data);
        break;
      default:
        break;
    }
  }

  void _sendInitialCommands() {
    if (!mounted || deviceMac.isEmpty) return;
    _send(.onCamera, data: deviceMac.toUint8List());
  }

  void _send(MsgType type, {Uint8List? data}) {
    AppState.shared.sendDeviceMessage(type, deviceMac: deviceMac, data: data);
  }

  // Stop timers, drop listeners, and tell the device to leave camera mode.
  void _cleanResources() {
    cameraMonitorTimer?.cancel();
    cameraMonitorTimer = null;
    WebSocketUtil.shared.removeObserver(tag);
    WebSocketUtil.shared.removeConnectionObserver(this);
    unawaited(_localMessages?.cancel());
    unawaited(_localConnections?.cancel());
    if (deviceMac.isNotEmpty) {
      _send(.offCamera, data: deviceMac.toUint8List());
    }
    // AudioEngineManager.shared.onAudioData = null;
    // AudioEngineManager.shared.stopPlayOpus();
    // AudioEngineManager.shared.stopRecording();
  }

  Widget imageView(Uint8List imageData) {
    if (imageData.isNotEmpty) {
      return Image.memory(
        imageData,
        gaplessPlayback: true,
        fit: BoxFit.cover,
        width: double.infinity,
        height: double.infinity,
      );
    } else {
      return Container(
        width: double.infinity,
        height: double.infinity,
        color: CupertinoColors.tertiarySystemBackground.resolveFrom(context),
      );
    }
  }

  @override
  Widget build(BuildContext context) {
    final mediaQueryPadding = MediaQuery.paddingOf(context);
    final orientation = MediaQuery.orientationOf(context);

    final trailing = Obx(
      () => Text(
        AppState.shared.isLocalConnection.value ? "Local Network" : "Remote",
      ),
    );

    return CupertinoPageScaffold(
      backgroundColor: CupertinoColors.systemGroupedBackground.resolveFrom(
        context,
      ),
      child: LayoutBuilder(
        builder: (context, constraints) {
          double maxWidth =
              constraints.maxWidth -
              (mediaQueryPadding.left + mediaQueryPadding.right);

          return CustomScrollView(
            slivers: [
              CupertinoSliverNavigationBar(
                backgroundColor:
                    ThemeSetting.cupertinoNavigationBarBackgroundColor(context),
                border: ThemeSetting.cupertinoNavigationBarBorder(context),
                padding: ThemeSetting.navigationBarPadding,
                largeTitle: const Text("CAMERA"),
                trailing: trailing,
              ),

              SliverPadding(
                padding: .only(
                  left: mediaQueryPadding.left,
                  right: mediaQueryPadding.right,
                  bottom: mediaQueryPadding.bottom,
                ),
                sliver: orientation == .landscape
                    ? SliverFillRemaining(
                        child: Row(
                          children: [
                            SizedBox(
                              width: (maxWidth - 10) / 2,
                              height: (maxWidth - 10 / 2) / 4 * 3,
                              child: Obx(() => imageView(cameraImage.value)),
                            ),
                            const Spacer(),
                            Container(
                              width: (maxWidth - 10) / 2,
                              height: double.infinity,
                              decoration: BoxDecoration(
                                color: CupertinoColors.tertiarySystemBackground
                                    .resolveFrom(context),
                                borderRadius: BorderRadius.circular(25),
                              ),
                              child: GridCoordinateJoystick(
                                minX: -1280,
                                maxX: 1280,
                                minY: 0,
                                maxY: 900,
                                padding: const EdgeInsets.all(25),
                                showMarking: false,
                                targetGridSize: 50,
                                buttonSize: 50,
                                point: const Offset(0, 450),
                                onImmediatelyRelease: saveMotionData,
                              ),
                            ),
                          ],
                        ),
                      )
                    : SliverList.list(
                        children: [
                          SizedBox(
                            width: maxWidth,
                            height: maxWidth / 4 * 3,
                            child: Obx(() => imageView(cameraImage.value)),
                          ),
                          Padding(
                            padding: const .all(20),
                            child: Container(
                              width: double.infinity,
                              height: constraints.maxHeight / 2.7,
                              decoration: BoxDecoration(
                                color: CupertinoColors.tertiarySystemBackground
                                    .resolveFrom(context),
                                borderRadius: BorderRadius.circular(25),
                              ),
                              child: GridCoordinateJoystick(
                                minX: -1280,
                                maxX: 1280,
                                minY: 0,
                                maxY: 900,
                                padding: const EdgeInsets.all(25),
                                showMarking: false,
                                targetGridSize: 50,
                                buttonSize: 50,
                                point: const Offset(0, 450),
                                onImmediatelyRelease: saveMotionData,
                              ),
                            ),
                          ),
                        ],
                      ),
              ),
            ],
          );
        },
      ),
    );
  }

  void saveMotionData(Offset point) {
    if (deviceMac.isNotEmpty) {
      final newDate = DateTime.now();
      final timeDiff = newDate.difference(sendScreenLastTime).inMilliseconds;
      if (timeDiff >= 200) {
        sendScreenLastTime = newDate;
        motionData.pitchServo.angle = point.dy.toInt();
        motionData.yawServo.angle = point.dx.toInt();
        final String jsonString = "$deviceMac${motionData.toString()}";
        final data = jsonString.toUint8List();
        _send(.controlMotion, data: data);
      }
    }
  }
}
