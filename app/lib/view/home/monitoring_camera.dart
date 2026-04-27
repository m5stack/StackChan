/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

import 'package:flutter/cupertino.dart';
import 'package:flutter/services.dart';
import 'package:get/get.dart';
import 'package:stack_chan/app_state.dart';
import 'package:stack_chan/model/expression_data.dart';
import 'package:stack_chan/network/web_socket_util.dart';
import 'package:stack_chan/util/audio_engine_manager.dart';
import 'package:stack_chan/util/extension.dart';

import '../util/grid_coordinate_joystick.dart';

class MonitoringCamera extends StatefulWidget {
  const MonitoringCamera({super.key});

  @override
  State<StatefulWidget> createState() => _MonitoringCameraState();
}

class _MonitoringCameraState extends State<MonitoringCamera> {
  final String tag = "MonitoringCamera";

  MotionData motionData = MotionData(
    pitchServo: MotionDataItem(),
    yawServo: MotionDataItem(),
  );

  DateTime sendScreenLastTime = DateTime.now();

  Rx<Uint8List> cameraImage = Rx(Uint8List(0));
  RxBool onMic = RxBool(false);

  @override
  void initState() {
    super.initState();
    initCameraAndSocket();
    WebSocketUtil.shared.connectionSuccessful = () {
      initCameraAndSocket();
    };
  }

  @override
  void dispose() {
    _cleanResources();
    super.dispose();
  }

  //====================== coreinitmethod ======================
  Future<void> initCameraAndSocket() async {
    WebSocketUtil.shared.removeObserver(tag);
    WebSocketUtil.shared.addObserver(tag, (message) {
      if (message is Uint8List) {
        final result = AppState.shared.parseMessage(message);
        final msgType = result.$1;
        final parsedData = result.$2;
        if (msgType != null) {
          switch (msgType) {
            case .jpeg:
              if (parsedData != null) {
                cameraImage.value = parsedData;
              }
              break;
            case .opus:
              if (parsedData != null) {
                AudioEngineManager.shared.playOpus(parsedData);
              }
              break;
            default:
              break;
          }
        }
      }
    });

    //re打开Camera + audioStream(keyfix:exitAgain进must重发)
    if (AppState.shared.deviceMac.isNotEmpty) {
      AppState.shared.sendWebSocketMessage(
        .onCamera,
        data: AppState.shared.deviceMac.toUint8List(),
      );
      // AppState.shared.sendWebSocketMessage(
      //   .onAudio,
      //   data: AppState.shared.deviceMac.toUint8List(),
      // );
    }

    //audiodatasend
    // AudioEngineManager.shared.onAudioData = (opusData) {
    //   final bytesBuilder = BytesBuilder();
    //   bytesBuilder.add(AppState.shared.deviceMac.toUint8List());
    //   bytesBuilder.add(opusData);
    //   final sendData = bytesBuilder.toBytes();
    //   AppState.shared.sendWebSocketMessage(.opus, data: sendData);
    // };
  }

  //cleanAsset / Resource源
  void _cleanResources() {
    WebSocketUtil.shared.removeObserver(tag);
    if (AppState.shared.deviceMac.isNotEmpty) {
      AppState.shared.sendWebSocketMessage(
        .offCamera,
        data: AppState.shared.deviceMac.toUint8List(),
      );
      // AppState.shared.sendWebSocketMessage(
      //   .offAudio,
      //   data: AppState.shared.deviceMac.toUint8List(),
      // );
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
        color: CupertinoColors.systemGroupedBackground.resolveFrom(context),
      );
    }
  }

  @override
  Widget build(BuildContext context) {
    return CupertinoPageScaffold(
      backgroundColor: CupertinoColors.systemBackground.resolveFrom(context),
      navigationBar: CupertinoNavigationBar.large(
        backgroundColor: CupertinoColors.systemBackground.resolveFrom(context),
        largeTitle: const Text("CAMERA"),
      ),
      child: LayoutBuilder(
        builder: (context, constraints) {
          return Column(
            children: [
              SizedBox(
                width: constraints.maxWidth,
                height: constraints.maxWidth / 4 * 3,
                child: Obx(() => imageView(cameraImage.value)),
              ),
              const Spacer(),
              Padding(
                padding: const EdgeInsets.all(15),
                child: Container(
                  width: double.infinity,
                  height: 300,
                  decoration: BoxDecoration(
                    color: CupertinoColors.systemGroupedBackground.resolveFrom(
                      context,
                    ),
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
                    point: Offset(0, 450),
                    onImmediatelyRelease: saveMotionData,
                  ),
                ),
              ),
              const Spacer(),
              SizedBox(height: MediaQuery.of(context).padding.bottom),
            ],
          );
        },
      ),
    );
  }

  void saveMotionData(Offset point) {
    if (AppState.shared.deviceMac.isNotEmpty) {
      final newDate = DateTime.now();
      final timeDiff = newDate.difference(sendScreenLastTime).inMilliseconds;
      if (timeDiff >= 200) {
        sendScreenLastTime = newDate;
        motionData.pitchServo.angle = point.dy.toInt();
        motionData.yawServo.angle = point.dx.toInt();
        final String jsonString =
            "${AppState.shared.deviceMac}${motionData.toString()}";
        final data = jsonString.toUint8List();
        AppState.shared.sendWebSocketMessage(.controlMotion, data: data);
      }
    }
  }
}
