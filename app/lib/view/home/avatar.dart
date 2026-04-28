/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

import 'dart:io';
import 'dart:typed_data';
import 'dart:ui' as ui;

import 'package:flutter/cupertino.dart';
import 'package:flutter_svg/flutter_svg.dart';
import 'package:get/get.dart';
import 'package:stack_chan/app_state.dart';
import 'package:stack_chan/model/dance_list.dart';
import 'package:stack_chan/network/web_socket_util.dart';
import 'package:stack_chan/util/custom_colors.dart';
import 'package:stack_chan/util/extension.dart';
import 'package:stack_chan/view/util/stack_chan_face_view.dart';

import '../../model/expression_data.dart';
import '../util/stack_chan_ar_view.dart';

class Avatar extends StatefulWidget {
  const Avatar({super.key, required this.deviceMac});

  final String deviceMac;

  @override
  State<StatefulWidget> createState() => _AvatarViewState();
}

class AvatarModel extends GetxController {
  RxInt decorate = RxInt(1);
  RxBool showPhoneScreen = RxBool(false);
  Rx<Uint8List> cameraImage = Rx(Uint8List(0));
  RxBool microphone = RxBool(false);
}

class _AvatarViewState extends State<Avatar> {
  final String tag = "Avatar";

  late final AvatarModel model;
  DateTime sendScreenLastTime = DateTime.now();

  @override
  void dispose() {
    WebSocketUtil.shared.removeObserver(tag);
    AppState.shared.sendWebSocketMessage(
      .offPhoneScreen,
      data: widget.deviceMac.toUint8List(),
    );
    AppState.shared.sendWebSocketMessage(
      .offCamera,
      data: widget.deviceMac.toUint8List(),
    );
    if (widget.deviceMac != AppState.shared.deviceMac) {
      AppState.shared.sendWebSocketMessage(.hangupCall);
    }
    model.onClose();
    super.dispose();
  }

  @override
  void initState() {
    super.initState();
    model = AvatarModel();
    initCameraAndSocket();
    WebSocketUtil.shared.connectionSuccessful = () {
      initCameraAndSocket();
    };
  }

  void initCameraAndSocket() {
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
                model.cameraImage.value = parsedData;
              }
              break;
            case .hangupCall:
              break;
            case .opus:
              break;
            default:
              break;
          }
        }
      }
    });
    AppState.shared.sendWebSocketMessage(
      .onCamera,
      data: widget.deviceMac.toUint8List(),
    );
  }

  @override
  Widget build(BuildContext context) {
    return CupertinoPageScaffold(
      backgroundColor: CupertinoColors.black,
      navigationBar: CupertinoNavigationBar(
        middle: Text("Avatar", style: TextStyle(color: CupertinoColors.white)),
        backgroundColor: CustomColors.transparent,
        automaticBackgroundVisibility: false,
        enableBackgroundFilterBlur: false,
        border: null,
        brightness: .dark,
      ),
      child: LayoutBuilder(
        builder: (context, constraints) {
          return Stack(
            alignment: .bottomCenter,
            children: [
              Column(
                children: [
                  Container(
                    width: .infinity,
                    height: constraints.maxWidth / 4 * 3,
                    color: CupertinoColors.systemGrey,
                    child: Obx(() {
                      if (model.cameraImage.value.isEmpty) {
                        return const CupertinoActivityIndicator();
                      }
                      return Image.memory(
                        model.cameraImage.value,
                        gaplessPlayback: true,
                        fit: BoxFit.cover,
                      );
                    }),
                  ),
                  Expanded(
                    child: Obx(
                      () => Platform.isIOS
                          ? StackChanArView(
                              decorate: model.decorate.value,
                              captureScreen: model.showPhoneScreen.value,
                              onCallback: sendDanceData,
                              onFrameCallback: compressMobilePhoneScreen,
                            )
                          : StackChanFaceView(
                              captureScreen: model.showPhoneScreen.value,
                              onCallback: sendDanceData,
                              onFrameCallback: compressMobilePhoneScreen,
                            ),
                    ),
                  ),
                ],
              ),
              Padding(
                padding: .only(
                  left: 15,
                  right: 15,
                  top: 15,
                  bottom: MediaQuery.of(context).padding.bottom + 15,
                ),
                child: Row(
                  spacing: 15,
                  children: [
                    if (Platform.isIOS)
                      CupertinoButton(
                        padding: .all(22),
                        color: CupertinoColors.black.withValues(alpha: 0.5),
                        borderRadius: .circular(100),
                        child: SizedBox(
                          width: 66,
                          height: 66,
                          child: Center(
                            child: Column(
                              mainAxisSize: .min,
                              spacing: 5,
                              children: [
                                SizedBox(
                                  width: 44,
                                  height: 44,
                                  child: Align(
                                    alignment: .center,
                                    child: Obx(() {
                                      switch (model.decorate.value) {
                                        case 0:
                                          return SvgPicture.asset(
                                            "assets/slash.circle.svg",
                                            width: .infinity,
                                            height: .infinity,
                                            colorFilter: .mode(
                                              CupertinoColors.white,
                                              .srcIn,
                                            ),
                                          );
                                        case 1:
                                          return Image.asset(
                                            "assets/image1.png",
                                            width: .infinity,
                                            height: .infinity,
                                          );
                                        case 2:
                                          return Text(
                                            "🐽",
                                            style: TextStyle(
                                              fontSize: 44,
                                              height: 1.1,
                                            ),
                                          );
                                        default:
                                          return Text(
                                            "🎲",
                                            style: TextStyle(
                                              fontSize: 44,
                                              height: 1.1,
                                            ),
                                          );
                                      }
                                    }),
                                  ),
                                ),
                                Text(
                                  "Mask",
                                  textScaler: .noScaling,
                                  textAlign: .center,
                                  style: TextStyle(
                                    color: CupertinoColors.white,
                                    fontSize: 12,
                                    fontWeight: .bold,
                                  ),
                                ),
                              ],
                            ),
                          ),
                        ),
                        onPressed: () {
                          if (model.decorate.value == 0) {
                            model.decorate.value = 1;
                          } else if (model.decorate.value == 1) {
                            model.decorate.value = 2;
                          } else if (model.decorate.value == 2) {
                            model.decorate.value = 0;
                          }
                        },
                      ),
                    Spacer(),
                    // CupertinoButton(
                    //   padding: .all(22),
                    //   color: CupertinoColors.black.withValues(alpha: 0.5),
                    //   borderRadius: .circular(50),
                    //   child: SizedBox(
                    //     width: 44,
                    //     height: 44,
                    //     child: Center(
                    //       child: Obx(
                    //         () => SvgPicture.asset(
                    //           model.microphone.value
                    //               ? "assets/microphone.svg"
                    //               : "assets/microphone.slash.svg",
                    //           width: .infinity,
                    //           height: .infinity,
                    //           colorFilter: .mode(CupertinoColors.white, .srcIn),
                    //         ),
                    //       ),
                    //     ),
                    //   ),
                    //   onPressed: () {
                    //     model.microphone.toggle();
                    //   },
                    // ),
                    CupertinoButton(
                      padding: .all(22),
                      color: CupertinoColors.black.withValues(alpha: 0.5),
                      borderRadius: .circular(100),
                      child: SizedBox(
                        width: 66,
                        height: 66,
                        child: Center(
                          child: Column(
                            mainAxisSize: .min,
                            spacing: 6,
                            children: [
                              SizedBox(
                                width: 44,
                                height: 44,
                                child: Center(
                                  child: Obx(
                                    () => SvgPicture.asset(
                                      "assets/iphone.gen1.badge.play.svg",
                                      width: .infinity,
                                      height: .infinity,
                                      colorFilter: .mode(
                                        model.showPhoneScreen.value
                                            ? CupertinoTheme.of(
                                                context,
                                              ).primaryColor
                                            : CupertinoColors.white,
                                        .srcIn,
                                      ),
                                    ),
                                  ),
                                ),
                              ),
                              Text(
                                "Mirror",
                                textScaler: .noScaling,
                                textAlign: .center,
                                style: TextStyle(
                                  color: CupertinoColors.white,
                                  fontSize: 12,
                                  fontWeight: .bold,
                                ),
                              ),
                            ],
                          ),
                        ),
                      ),
                      onPressed: () {
                        model.showPhoneScreen.toggle();
                        if (model.showPhoneScreen.value) {
                          AppState.shared.sendWebSocketMessage(
                            .onPhoneScreen,
                            data: widget.deviceMac.toUint8List(),
                          );
                        } else {
                          AppState.shared.sendWebSocketMessage(
                            .offPhoneScreen,
                            data: widget.deviceMac.toUint8List(),
                          );
                        }
                      },
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

  void sendDanceData(DanceData data) {
    final expressionData = ExpressionData(
      leftEye: data.leftEye,
      rightEye: data.rightEye,
      mouth: data.mouth,
    );
    final expressionJsonString = widget.deviceMac + expressionData.toString();
    final expressionMessageData = expressionJsonString.toUint8List();
    AppState.shared.sendWebSocketMessage(
      .controlAvatar,
      data: expressionMessageData,
    );

    final motionData = MotionData(
      pitchServo: data.pitchServo,
      yawServo: data.yawServo,
    );
    final motionJsonString = widget.deviceMac + motionData.toString();
    final motionMessageData = motionJsonString.toUint8List();
    AppState.shared.sendWebSocketMessage(
      .controlMotion,
      data: motionMessageData,
    );
  }

  void compressMobilePhoneScreen(Uint8List imageData) async {
    if (imageData.isEmpty) return;
    final newDate = DateTime.now();
    final timeDiff = newDate.difference(sendScreenLastTime).inMilliseconds;
    if (timeDiff >= 500) {
      sendScreenLastTime = newDate;
      Uint8List? newImageData = await imageData.compress(
        resolutionSize: ui.Size(320, 240),
        memorySize: 0.02,
        cropCenter: true,
      );
      if (newImageData != null) {
        final macBytes = widget.deviceMac.toUint8List();
        final payload = Uint8List(macBytes.length + newImageData.length);
        payload.setAll(0, macBytes);
        payload.setAll(macBytes.length, newImageData);
        AppState.shared.sendWebSocketMessage(.jpeg, data: payload);
      }
    }
  }
}
