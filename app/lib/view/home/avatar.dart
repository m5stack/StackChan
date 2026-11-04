/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

import 'dart:async';
import 'dart:io';
import 'dart:typed_data';
import 'dart:ui' as ui;

import 'package:cupertino_ui/cupertino_ui.dart';
import 'package:stack_chan/view/util/theme_setting.dart';
import 'package:flutter_svg/flutter_svg.dart';
import 'package:get/get.dart';
import 'package:stack_chan/app_state.dart';
import 'package:stack_chan/model/dance_list.dart';
import 'package:stack_chan/model/msg_type.dart';
import 'package:stack_chan/network/local_area_network_service.dart';
import 'package:stack_chan/network/web_socket_util.dart';
import 'package:stack_chan/util/audio_engine_manager.dart';
import 'package:stack_chan/util/extension.dart';
import 'package:stack_chan/view/util/stack_chan_face_view.dart';

import '../../model/expression_data.dart';
import '../../util/value_constant.dart';
import '../util/stack_chan_ar_view.dart';

class Avatar extends StatefulWidget {
  const Avatar({super.key, required this.deviceMac});

  final String deviceMac;

  @override
  State<StatefulWidget> createState() => _AvatarViewState();
}

enum CallPhase { idle, active }

class AvatarModel extends GetxController {
  RxInt decorate = RxInt(1);
  RxBool showPhoneScreen = RxBool(false);
  Rx<Uint8List> cameraImage = Rx(Uint8List(0));
  RxBool microphone = RxBool(false);
  Rx<CallPhase> callPhase = Rx(CallPhase.idle);
}

class _AvatarViewState extends State<Avatar> {
  late final String tag = "Avatar-${identityHashCode(this)}";

  late final AvatarModel model;
  DateTime sendScreenLastTime = DateTime.now();
  DateTime lastJpegReceivedTime = DateTime.now();
  Timer? cameraMonitorTimer;
  Timer? _callHealthTimer;
  bool _startingAudio = false;
  StreamSubscription<LocalAreaNetworkMessage>? _localMessages;
  StreamSubscription<bool>? _localConnections;

  @override
  void dispose() {
    cameraMonitorTimer?.cancel();
    cameraMonitorTimer = null;
    _callHealthTimer?.cancel();
    unawaited(_endCall(notifyPeer: true));
    WebSocketUtil.shared.removeObserver(tag);
    WebSocketUtil.shared.removeConnectionObserver(this);
    unawaited(_localMessages?.cancel());
    unawaited(_localConnections?.cancel());
    _send(.offPhoneScreen, data: widget.deviceMac.toUint8List());
    _send(.offCamera, data: widget.deviceMac.toUint8List());
    // Leaving AVATAR releases the LAN service; it stops once no page holds it.
    unawaited(LocalAreaNetworkService.instance.releaseForPage());
    model.onClose();
    super.dispose();
  }

  @override
  void initState() {
    super.initState();
    model = AvatarModel();
    final lan = LocalAreaNetworkService.instance;
    // Retain the LAN service while AVATAR is open and release it on exit.
    unawaited(lan.retainForPage());
    _localMessages = lan.messages.listen((message) {
      if (lan.canSendTo(widget.deviceMac)) {
        _handleMessage(message.type, message.payload);
      }
    });
    _localConnections = lan.connectionStates.listen((connected) {
      if (!connected || lan.canSendTo(widget.deviceMac)) _sendInitialCommands();
    });
    initCameraAndSocket();
    startCameraMonitor();
    WebSocketUtil.shared.addConnectionObserver(this, () {
      final lanActive = lan.canSendTo(widget.deviceMac);
      if (!lanActive && !WebSocketUtil.shared.isConnected) {
        unawaited(_endCall(notifyPeer: false));
        return;
      }
      if (!lanActive) _sendInitialCommands();
    });
    initPromptWindow();
  }

  Future<void> initPromptWindow() async {
    bool? isPromptWindowVisible = await AppState.asyncPrefs.getBool(
      ValueConstant.avatarPromptWindow,
    );
    if (isPromptWindowVisible == null || isPromptWindowVisible == false) {
      await AppState.asyncPrefs.setBool(ValueConstant.avatarPromptWindow, true);
      if (mounted) {
        AppState.shared.showSwitchDeviceAvatar(context);
      }
    }
  }

  void startCameraMonitor() {
    cameraMonitorTimer?.cancel();
    cameraMonitorTimer = Timer.periodic(const Duration(seconds: 3), (_) {
      if (!mounted || widget.deviceMac.isEmpty) return;
      final now = DateTime.now();
      final diffSeconds = now.difference(lastJpegReceivedTime).inSeconds;
      if (diffSeconds >= 3) {
        _send(.onCamera, data: widget.deviceMac.toUint8List());
      }
    });
  }

  void initCameraAndSocket() {
    WebSocketUtil.shared.removeObserver(tag);
    WebSocketUtil.shared.addObserver(tag, (message) {
      if (message is! Uint8List) return;
      final result = AppState.shared.parseMessage(message);
      final msgType = result.$1;
      final parsedData = result.$2;
      final lanActive = LocalAreaNetworkService.instance.canSendTo(
        widget.deviceMac,
      );
      final isCallControl =
          msgType == MsgType.refuseCall || msgType == MsgType.hangupCall;
      // Call-control messages such as a remote hang-up must be handled even when LAN is connected.
      if (!lanActive || isCallControl) {
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
        model.cameraImage.value = data;
        break;
      case MsgType.opus:
        if (data.isNotEmpty) {
          unawaited(AudioEngineManager.shared.playOpus(data));
        }
        break;
      case MsgType.refuseCall:
      case MsgType.hangupCall:
        unawaited(_endCall(notifyPeer: false));
        break;
      default:
        break;
    }
  }

  /// Restores the page defaults on first entry, LAN connect/reconnect/disconnect, and remote reconnect.
  void _sendInitialCommands() {
    if (!mounted) return;
    _send(.onCamera, data: widget.deviceMac.toUint8List());
    if (model.showPhoneScreen.value) {
      _send(.onPhoneScreen, data: widget.deviceMac.toUint8List());
    }
  }

  void _send(MsgType type, {Uint8List? data}) {
    AppState.shared.sendDeviceMessage(
      type,
      deviceMac: widget.deviceMac,
      data: data,
    );
  }

  Uint8List _macPayload() => widget.deviceMac.toUint8List();

  void _sendAudioControl(MsgType type) {
    final mac = _macPayload();
    // The remote channel manages the server-side audio subscription; the LAN channel tells the firmware to start or stop immediately.
    AppState.shared.sendWebSocketMessage(type, data: mac);
    if (LocalAreaNetworkService.instance.canSendTo(widget.deviceMac)) {
      _send(type, data: mac);
    }
  }

  Uint8List _audioPayload(Uint8List opusData) {
    final mac = _macPayload();
    final payload = Uint8List(mac.length + opusData.length);
    payload.setAll(0, mac);
    payload.setAll(mac.length, opusData);
    return payload;
  }

  // ignore: unused_element
  Future<void> _toggleCall() async {
    if (model.callPhase.value == CallPhase.active) {
      await _endCall(notifyPeer: true);
    } else {
      await _startAudioCall();
    }
  }

  Future<void> _startAudioCall() async {
    if (_startingAudio || model.callPhase.value == CallPhase.active) return;
    if (_macPayload().length != 12) {
      await _showCallAlert(
        "Unable to start audio",
        "Invalid device MAC address.",
      );
      return;
    }
    final lanActive = LocalAreaNetworkService.instance.canSendTo(
      widget.deviceMac,
    );
    if (!lanActive && !WebSocketUtil.shared.isConnected) {
      await _showCallAlert(
        "Unable to start audio",
        "The device is not connected.",
      );
      return;
    }

    _startingAudio = true;
    AudioEngineManager.shared.onAudioData = (opusData) {
      if (model.callPhase.value != CallPhase.active || opusData.isEmpty) return;
      _send(.opus, data: _audioPayload(opusData));
    };

    final started = await AudioEngineManager.shared.startRecording();
    if (!started || !mounted) {
      AudioEngineManager.shared.onAudioData = null;
      _startingAudio = false;
      if (mounted) {
        await _showCallAlert(
          "Microphone unavailable",
          "Allow microphone access to start audio.",
        );
      }
      await _endCall(notifyPeer: false);
      return;
    }

    _startingAudio = false;
    model.callPhase.value = CallPhase.active;
    model.microphone.value = true;
    _sendAudioControl(.onAudio);
    _callHealthTimer?.cancel();
    _callHealthTimer = Timer.periodic(const Duration(seconds: 2), (_) {
      if (model.callPhase.value != CallPhase.active) return;
      final lanActive = LocalAreaNetworkService.instance.canSendTo(
        widget.deviceMac,
      );
      if (!lanActive && !WebSocketUtil.shared.isConnected) {
        unawaited(_endCall(notifyPeer: false));
      }
    });
  }

  Future<void> _endCall({required bool notifyPeer}) async {
    final wasAudioActive =
        model.callPhase.value == CallPhase.active || _startingAudio;

    _callHealthTimer?.cancel();
    _callHealthTimer = null;
    _startingAudio = false;
    model.callPhase.value = CallPhase.idle;
    model.microphone.value = false;
    AudioEngineManager.shared.onAudioData = null;

    if (wasAudioActive) {
      await AudioEngineManager.shared.stopRecording();
      await AudioEngineManager.shared.stopPlayOpus();
    }
    if (notifyPeer && wasAudioActive) {
      _sendAudioControl(.offAudio);
    }
  }

  Future<void> _showCallAlert(String title, String message) {
    if (!mounted) return Future<void>.value();
    return showCupertinoDialog<void>(
      context: context,
      builder: (_) => CupertinoAlertDialog(
        title: Text(title),
        content: Text(message),
        actions: [
          CupertinoDialogAction(
            isDefaultAction: true,
            onPressed: () => Navigator.of(context).pop(),
            child: const Text("OK"),
          ),
        ],
      ),
    );
  }

  @override
  Widget build(BuildContext context) {
    final mediaQueryPadding = MediaQuery.paddingOf(context);
    final isLandscape = MediaQuery.orientationOf(context) == .landscape;
    return CupertinoPageScaffold(
      backgroundColor: CupertinoColors.systemGroupedBackground.resolveFrom(
        context,
      ),
      navigationBar: CupertinoNavigationBar(
        backgroundColor: CupertinoColors.systemGroupedBackground
            .resolveFrom(context)
            .withValues(alpha: 0.01),
        border: null,
        padding: ThemeSetting.navigationBarPadding,
        middle: const Text(
          "Avatar",
          style: TextStyle(color: CupertinoColors.white),
        ),
        automaticBackgroundVisibility: false,
        enableBackgroundFilterBlur: false,
        brightness: .dark,
        trailing: Obx(
          () => Text(
            AppState.shared.isLocalConnection.value
                ? "Local Network"
                : "Remote",
            style: const TextStyle(color: CupertinoColors.white),
          ),
        ),
      ),
      child: LayoutBuilder(
        builder: (context, constraints) {
          final cameraView = _cameraView();
          // In landscape the action bar sits inside the right-hand AR pane, so the left safe area is not needed.
          final arPane = _arPane(
            isLandscape
                ? mediaQueryPadding.copyWith(left: 0)
                : mediaQueryPadding,
          );
          // Landscape: camera on the left, AR view on the right.
          if (isLandscape) {
            return Row(
              children: [
                Expanded(child: cameraView),
                Expanded(child: arPane),
              ],
            );
          }
          // Portrait: stack the camera view above the AR view and keep the camera at 4:3.
          return Column(
            children: [
              SizedBox(
                width: .infinity,
                height: constraints.maxWidth / 4 * 3,
                child: cameraView,
              ),
              Expanded(child: arPane),
            ],
          );
        },
      ),
    );
  }

  /// Camera feed from the device.
  Widget _cameraView() {
    return ColoredBox(
      color: CupertinoColors.systemGrey.resolveFrom(context),
      child: Obx(() {
        if (model.cameraImage.value.isEmpty) {
          return const Center(child: CupertinoActivityIndicator());
        }
        return Image.memory(
          model.cameraImage.value,
          gaplessPlayback: true,
          fit: BoxFit.cover,
          width: .infinity,
          height: .infinity,
        );
      }),
    );
  }

  /// Local AR view (ARKit on iOS, the expression view elsewhere) with the action bar overlaid at the bottom.
  Widget _arPane(EdgeInsets safeArea) {
    return Container(
      color: CupertinoColors.systemGrey.resolveFrom(context),
      child: Stack(
        fit: StackFit.expand,
        children: [
          Obx(
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
          Align(
            alignment: .bottomCenter,
            child: Padding(
              padding: .only(
                left: 20 + safeArea.left,
                right: 20 + safeArea.right,
                top: 20,
                bottom: safeArea.bottom + 20,
              ),
              child: _actionBar(),
            ),
          ),
        ],
      ),
    );
  }

  /// Bottom action bar that follows the AR view.
  Widget _actionBar() {
    return Row(
      spacing: 10,
      children: [
        if (Platform.isIOS)
          bottomButtonWidget(
            Obx(() {
              switch (model.decorate.value) {
                case 0:
                  return SvgPicture.asset(
                    "assets/slash.circle.svg",
                    width: 44,
                    height: 44,
                    colorFilter: const .mode(CupertinoColors.white, .srcIn),
                  );
                case 1:
                  return Image.asset(
                    "assets/image1.png",
                    width: 44,
                    height: 44,
                  );
                case 2:
                  return const Text(
                    "🐽",
                    style: TextStyle(fontSize: 44, height: 1.1),
                  );
                default:
                  return const Text(
                    "🎲",
                    style: TextStyle(fontSize: 44, height: 1.1),
                  );
              }
            }),
            CupertinoColors.white,
            "Mask",
            () {
              if (model.decorate.value == 0) {
                model.decorate.value = 1;
              } else if (model.decorate.value == 1) {
                model.decorate.value = 2;
              } else if (model.decorate.value == 2) {
                model.decorate.value = 0;
              }
            },
          ),
        // CupertinoButton(
        //   padding: .all(22),
        //   color: CupertinoColors.black.withValues(alpha: 0.5),
        //   borderRadius: .circular(100),
        //   child: SizedBox(
        //     width: 66,
        //     height: 66,
        //     child: Center(
        //       child: Column(
        //         mainAxisSize: .min,
        //         spacing: 5,
        //         children: [
        //           SizedBox(
        //             width: 44,
        //             height: 44,
        //             child: Align(
        //               alignment: .center,
        //               child: Obx(() {
        //                 switch (model.decorate.value) {
        //                   case 0:
        //                     return SvgPicture.asset(
        //                       "assets/slash.circle.svg",
        //                       width: .infinity,
        //                       height: .infinity,
        //                       colorFilter: .mode(
        //                         CupertinoColors.white,
        //                         .srcIn,
        //                       ),
        //                     );
        //                   case 1:
        //                     return Image.asset(
        //                       "assets/image1.png",
        //                       width: .infinity,
        //                       height: .infinity,
        //                     );
        //                   case 2:
        //                     return Text(
        //                       "🐽",
        //                       style: TextStyle(
        //                         fontSize: 44,
        //                         height: 1.1,
        //                       ),
        //                     );
        //                   default:
        //                     return Text(
        //                       "🎲",
        //                       style: TextStyle(
        //                         fontSize: 44,
        //                         height: 1.1,
        //                       ),
        //                     );
        //                 }
        //               }),
        //             ),
        //           ),
        //           Text(
        //             "Mask",
        //             textScaler: .noScaling,
        //             textAlign: .center,
        //             style: TextStyle(
        //               color: CupertinoColors.white,
        //               fontSize: 12,
        //               fontWeight: .bold,
        //             ),
        //           ),
        //         ],
        //       ),
        //     ),
        //   ),
        //   onPressed: () {
        //     if (model.decorate.value == 0) {
        //       model.decorate.value = 1;
        //     } else if (model.decorate.value == 1) {
        //       model.decorate.value = 2;
        //     } else if (model.decorate.value == 2) {
        //       model.decorate.value = 0;
        //     }
        //   },
        // ),
        const Spacer(),

        // Audio entry point is hidden until the audio feature is fully implemented.
        // Obx(() {
        //   final phase = model.callPhase.value;
        //   final active = phase == CallPhase.active;
        //   return bottomButtonWidget(
        //     SvgPicture.asset(
        //       active
        //           ? "assets/microphone.svg"
        //           : "assets/microphone.slash.svg",
        //       width: 44,
        //       height: 44,
        //       colorFilter: .mode(
        //         active
        //             ? CupertinoTheme.of(context).primaryColor
        //             : CupertinoColors.white,
        //         .srcIn,
        //       ),
        //     ),
        //     active
        //         ? CupertinoTheme.of(context).primaryColor
        //         : CupertinoColors.white,
        //     active ? "End Call" : "Call",
        //     () => unawaited(_toggleCall()),
        //   );
        // }),
        Obx(
          () => bottomButtonWidget(
            SvgPicture.asset(
              "assets/iphone.gen1.badge.play.svg",
              width: 44,
              height: 44,
              colorFilter: .mode(
                model.showPhoneScreen.value
                    ? CupertinoTheme.of(context).primaryColor
                    : CupertinoColors.white,
                .srcIn,
              ),
            ),
            model.showPhoneScreen.value
                ? CupertinoTheme.of(context).primaryColor
                : CupertinoColors.white,
            "Mirror",
            () {
              model.showPhoneScreen.toggle();
              if (model.showPhoneScreen.value) {
                _send(.onPhoneScreen, data: widget.deviceMac.toUint8List());
              } else {
                _send(.offPhoneScreen, data: widget.deviceMac.toUint8List());
              }
            },
          ),
        ),

        // CupertinoButton(
        //   padding: .all(22),
        //   color: CupertinoColors.black.withValues(alpha: 0.5),
        //   borderRadius: .circular(100),
        //   child: SizedBox(
        //     width: 66,
        //     height: 66,
        //     child: Center(
        //       child: Column(
        //         mainAxisSize: .min,
        //         spacing: 6,
        //         children: [
        //           SizedBox(
        //             width: 44,
        //             height: 44,
        //             child: Center(
        //               child: Obx(
        //                 () => SvgPicture.asset(
        //                   "assets/iphone.gen1.badge.play.svg",
        //                   width: .infinity,
        //                   height: .infinity,
        //                   colorFilter: .mode(
        //                     model.showPhoneScreen.value
        //                         ? CupertinoTheme.of(
        //                             context,
        //                           ).primaryColor
        //                         : CupertinoColors.white,
        //                     .srcIn,
        //                   ),
        //                 ),
        //               ),
        //             ),
        //           ),
        //           Text(
        //             "Mirror",
        //             textScaler: .noScaling,
        //             textAlign: .center,
        //             style: TextStyle(
        //               color: CupertinoColors.white,
        //               fontSize: 12,
        //               fontWeight: .bold,
        //             ),
        //           ),
        //         ],
        //       ),
        //     ),
        //   ),
        //   onPressed: () {
        //     model.showPhoneScreen.toggle();
        //     if (model.showPhoneScreen.value) {
        //       _send(
        //         .onPhoneScreen,
        //         data: widget.deviceMac.toUint8List(),
        //       );
        //     } else {
        //       _send(
        //         .offPhoneScreen,
        //         data: widget.deviceMac.toUint8List(),
        //       );
        //     }
        //   },
        // ),
      ],
    );
  }

  Widget bottomButtonWidget(
    Widget icon,
    Color foregroundColor,
    String text,
    Function() onPressed,
  ) {
    return SizedBox(
      height: 90,
      width: 84,
      child: CupertinoButton.tinted(
        borderRadius: .circular(ThemeSetting.mediumCardRadius),
        color: CupertinoColors.white,
        onPressed: onPressed,
        padding: .zero,
        child: Center(
          child: Column(
            mainAxisSize: .min,
            spacing: 10,
            children: [
              icon,
              Text(
                text,
                textScaler: .noScaling,
                textAlign: .center,
                style: TextStyle(
                  fontSize: 12,
                  fontWeight: .bold,
                  color: foregroundColor,
                ),
              ),
            ],
          ),
        ),
      ),
    );
  }

  void sendDanceData(DanceData data) {
    if (!mounted) return;
    final expressionData = ExpressionData(
      leftEye: data.leftEye,
      rightEye: data.rightEye,
      mouth: data.mouth,
    );
    final expressionJsonString = widget.deviceMac + expressionData.toString();
    final expressionMessageData = expressionJsonString.toUint8List();
    _send(.controlAvatar, data: expressionMessageData);
    final motionData = MotionData(
      pitchServo: data.pitchServo,
      yawServo: data.yawServo,
    );
    final motionJsonString = widget.deviceMac + motionData.toString();
    final motionMessageData = motionJsonString.toUint8List();
    _send(.controlMotion, data: motionMessageData);
  }

  /// Mirror frames also prefer LAN for the downlink path.
  ///
  /// A LAN frame is capped at 65535 bytes including the 12-byte MAC prefix. When
  /// the device is reachable directly, compress at 320x240 first and drop the
  /// resolution if the frame is still too large, so as much traffic as possible
  /// stays on LAN. Only fall back to the remote path when LAN is unavailable or
  /// even the lowest resolution exceeds the limit.
  void compressMobilePhoneScreen(Uint8List imageData) async {
    if (!mounted || !model.showPhoneScreen.value || imageData.isEmpty) return;
    final newDate = DateTime.now();
    final timeDiff = newDate.difference(sendScreenLastTime).inMilliseconds;
    if (timeDiff < 500) return;
    sendScreenLastTime = newDate;
    final macBytes = widget.deviceMac.toUint8List();
    final lanConnected = LocalAreaNetworkService.instance.canSendTo(
      widget.deviceMac,
    );
    final attempts = lanConnected
        ? const [ui.Size(320, 240), ui.Size(240, 180), ui.Size(160, 120)]
        : const [ui.Size(320, 240)];
    Uint8List? compressed;
    for (final size in attempts) {
      final newImageData = await imageData.compress(
        resolutionSize: size,
        memorySize: 0.02,
        cropCenter: true,
      );
      if (!mounted || !model.showPhoneScreen.value || newImageData == null) {
        return;
      }
      compressed = newImageData;
      if (macBytes.length + newImageData.length <=
          LocalAreaNetworkMessage.maxPayloadSize) {
        break;
      }
    }
    if (compressed == null) return;
    final payload = Uint8List(macBytes.length + compressed.length);
    payload.setAll(0, macBytes);
    payload.setAll(macBytes.length, compressed);
    _send(.jpeg, data: payload);
  }
}
