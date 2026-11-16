/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

import 'dart:async';
import 'dart:typed_data';

import 'package:cupertino_ui/cupertino_ui.dart';
import 'package:stack_chan/app_state.dart';
import 'package:stack_chan/model/dance_list.dart';
import 'package:stack_chan/model/expression_data.dart';
import 'package:stack_chan/model/msg_type.dart';
import 'package:stack_chan/network/local_area_network_service.dart';
import 'package:stack_chan/network/web_socket_util.dart';
import 'package:stack_chan/util/extension.dart';
import 'package:stack_chan/util/value_constant.dart';
import 'package:stack_chan/view/util/grid_coordinate_joystick.dart';
import 'package:stack_chan/view/util/stackchan_robot_box.dart';
import 'package:get/get.dart';

import '../util/theme_setting.dart';

class Motion extends StatefulWidget {
  const Motion({super.key, this.scrollController});

  final ScrollController? scrollController;

  @override
  State<StatefulWidget> createState() => _MotionState();
}

class _MotionState extends State<Motion> {
  int _selectedIndex = 0;

  late ExpressionData avatarData;
  late MotionData motionData;

  final String tag = "Motion";

  @override
  void dispose() {
    WebSocketUtil.shared.removeObserver(tag);
    // Leaving MOTION releases the LAN service (publishing stops once no page holds it).
    unawaited(LocalAreaNetworkService.instance.releaseForPage());
    super.dispose();
  }

  @override
  void initState() {
    super.initState();
    avatarData = ExpressionData(
      leftEye: ExpressionItem(weight: 100),
      rightEye: ExpressionItem(weight: 100),
      mouth: ExpressionItem(weight: 0),
    );
    motionData = MotionData(
      pitchServo: MotionDataItem(),
      yawServo: MotionDataItem(),
    );
    // Entering MOTION starts the LAN service; leaving the page releases it.
    unawaited(LocalAreaNetworkService.instance.retainForPage());

    _send(.getAvatarPosture);

    WebSocketUtil.shared.addObserver(tag, (message) {
      if (message is Uint8List) {
        final result = AppState.shared.parseMessage(message);
        final msgType = result.$1;
        if (msgType != null) {
          switch (msgType) {
            case .getAvatarPosture:
              break;
            default:
              break;
          }
        }
      }
    });
    initPromptWindow();
  }

  Future<void> initPromptWindow() async {
    bool? isPromptWindowVisible = await AppState.asyncPrefs.getBool(
      ValueConstant.motionPromptWindow,
    );
    if (isPromptWindowVisible == null || isPromptWindowVisible == false) {
      await AppState.asyncPrefs.setBool(ValueConstant.motionPromptWindow, true);
      if (mounted) {
        AppState.shared.showSwitchDeviceAvatar(context);
      }
    }
  }

  void _saveAvatarData() {
    if (AppState.shared.deviceMac.isNotEmpty) {
      String jsonString = AppState.shared.deviceMac + avatarData.toString();
      _send(.controlAvatar, data: jsonString.toUint8List());
    }
  }

  void _saveMotionData() {
    if (AppState.shared.deviceMac.isNotEmpty) {
      String jsonString = AppState.shared.deviceMac + motionData.toString();
      _send(.controlMotion, data: jsonString.toUint8List());
    }
  }

  void _send(MsgType type, {Uint8List? data}) {
    final deviceMac = AppState.shared.deviceMac;
    if (deviceMac.isEmpty) return;
    AppState.shared.sendDeviceMessage(type, deviceMac: deviceMac, data: data);
  }

  @override
  Widget build(BuildContext context) {
    final padding = MediaQuery.paddingOf(context);
    return CupertinoPageScaffold(
      backgroundColor: CupertinoColors.systemGroupedBackground.resolveFrom(
        context,
      ),
      navigationBar: CupertinoNavigationBar(
        backgroundColor: ThemeSetting.cupertinoNavigationBarBackgroundColor(
          context,
        ),
        leading: const SizedBox.shrink(),
        border: ThemeSetting.cupertinoNavigationBarBorder(context),
        padding: ThemeSetting.navigationBarPadding,
        trailing: Row(
          mainAxisSize: .min,
          children: [
            Padding(
              padding: const .all(ThemeSetting.navigationBarButtonTextPadding),
              child: Obx(
                () => Text(
                  AppState.shared.isLocalConnection.value
                      ? "Local Network"
                      : "Remote",
                ),
              ),
            ),
            CupertinoButton(
              sizeStyle: .medium,
              onPressed: () {
                Navigator.maybePop(context);
              },
              child: const Icon(CupertinoIcons.xmark),
            ),
          ],
        ),
      ),
      child: ListView(
        controller: widget.scrollController,
        padding: .only(
          top: kMinInteractiveDimensionCupertino,
          bottom: padding.bottom + 10,
          left: padding.left,
          right: padding.right,
        ),
        children: [
          StackChanRobotBox(
            mirrorFace: true,
            width: double.infinity,
            height: 250,
            data: DanceData(
              leftEye: avatarData.leftEye,
              rightEye: avatarData.rightEye,
              mouth: avatarData.mouth,
              yawServo: motionData.yawServo,
              pitchServo: motionData.pitchServo,
              durationMs: 1000,
            ),
          ),
          const SizedBox(height: 10),
          Padding(
            padding: const .symmetric(horizontal: 20),
            child: Row(
              children: [
                Expanded(
                  child: CupertinoSlidingSegmentedControl(
                    children: const {0: Text("Motion"), 1: Text("Avatar")},
                    groupValue: _selectedIndex,
                    onValueChanged: (value) {
                      setState(() {
                        _selectedIndex = value!;
                      });
                    },
                  ),
                ),
                CupertinoButton(
                  sizeStyle: .medium,
                  onPressed: () {
                    setState(() {
                      if (_selectedIndex == 1) {
                        avatarData = ExpressionData(
                          leftEye: ExpressionItem(weight: 100),
                          rightEye: ExpressionItem(weight: 100),
                          mouth: ExpressionItem(weight: 0),
                        );
                        _saveAvatarData();
                      } else {
                        motionData = MotionData(
                          pitchServo: MotionDataItem(),
                          yawServo: MotionDataItem(),
                        );
                        _saveMotionData();
                      }
                    });
                  },
                  child: const Icon(CupertinoIcons.refresh),
                ),
                // CupertinoButton(
                //   sizeStyle: .medium,
                //   onPressed: () {
                //     setState(() {
                //       if (_selectedIndex == 1) {
                //         avatarData = ExpressionData(
                //           leftEye: ExpressionItem(weight: 100),
                //           rightEye: ExpressionItem(weight: 100),
                //           mouth: ExpressionItem(weight: 0),
                //         );
                //         _saveAvatarData();
                //       } else {
                //         motionData = MotionData(
                //           pitchServo: MotionDataItem(),
                //           yawServo: MotionDataItem(),
                //         );
                //         _saveMotionData();
                //       }
                //     });
                //   },
                //   child: const Icon(CupertinoIcons.refresh),
                // ),
              ],
            ),
          ),
          if (_selectedIndex == 1) _buildAvatarControls(context),
          if (_selectedIndex == 0) _buildMotionControls(context),
        ],
      ),
    );
  }

  Widget _buildAvatarControls(BuildContext context) {
    TextStyle titleStyle = TextStyle(
      color: CupertinoColors.label.resolveFrom(context),
      fontSize: 15,
    );
    TextStyle valueStyle = TextStyle(
      color: CupertinoColors.secondaryLabel.resolveFrom(context),
      fontSize: 15,
    );

    return Column(
      mainAxisSize: MainAxisSize.min,
      children: [
        CupertinoListSection.insetGrouped(
          header: const Text("Left Eye"),
          children: [
            _buildSlider(
              "x",
              avatarData.leftEye.x.toDouble(),
              -100,
              100,
              titleStyle,
              valueStyle,
              (val) {
                setState(() => avatarData.leftEye.x = val.toInt());
              },
              onDragEnd: _saveAvatarData,
            ),
            _buildSlider(
              "y",
              avatarData.leftEye.y.toDouble(),
              -100,
              100,
              titleStyle,
              valueStyle,
              (val) {
                setState(() => avatarData.leftEye.y = val.toInt());
              },
              onDragEnd: _saveAvatarData,
            ),
            _buildSlider(
              "rotation",
              avatarData.leftEye.rotation.toDouble(),
              -1800,
              1800,
              titleStyle,
              valueStyle,
              (val) {
                setState(() => avatarData.leftEye.rotation = val.toInt());
              },
              onDragEnd: _saveAvatarData,
            ),
            _buildSlider(
              "weight",
              avatarData.leftEye.weight.toDouble(),
              0,
              100,
              titleStyle,
              valueStyle,
              (val) {
                setState(() => avatarData.leftEye.weight = val.toInt());
              },
              onDragEnd: _saveAvatarData,
            ),
            _buildSlider(
              "size",
              avatarData.leftEye.size.toDouble(),
              -100,
              100,
              titleStyle,
              valueStyle,
              (val) {
                setState(() => avatarData.leftEye.size = val.toInt());
              },
              onDragEnd: _saveAvatarData,
            ),
          ],
        ),

        CupertinoListSection.insetGrouped(
          header: const Text("Right Eye"),
          children: [
            _buildSlider(
              "x",
              avatarData.rightEye.x.toDouble(),
              -100,
              100,
              titleStyle,
              valueStyle,
              (val) {
                setState(() => avatarData.rightEye.x = val.toInt());
              },
              onDragEnd: _saveAvatarData,
            ),
            _buildSlider(
              "y",
              avatarData.rightEye.y.toDouble(),
              -100,
              100,
              titleStyle,
              valueStyle,
              (val) {
                setState(() => avatarData.rightEye.y = val.toInt());
              },
              onDragEnd: _saveAvatarData,
            ),
            _buildSlider(
              "rotation",
              avatarData.rightEye.rotation.toDouble(),
              -1800,
              1800,
              titleStyle,
              valueStyle,
              (val) {
                setState(() => avatarData.rightEye.rotation = val.toInt());
              },
              onDragEnd: _saveAvatarData,
            ),
            _buildSlider(
              "weight",
              avatarData.rightEye.weight.toDouble(),
              0,
              100,
              titleStyle,
              valueStyle,
              (val) {
                setState(() => avatarData.rightEye.weight = val.toInt());
              },
              onDragEnd: _saveAvatarData,
            ),
            _buildSlider(
              "size",
              avatarData.rightEye.size.toDouble(),
              -100,
              100,
              titleStyle,
              valueStyle,
              (val) {
                setState(() => avatarData.rightEye.size = val.toInt());
              },
              onDragEnd: _saveAvatarData,
            ),
          ],
        ),

        CupertinoListSection.insetGrouped(
          header: const Text("Mouth"),
          children: [
            _buildSlider(
              "x",
              avatarData.mouth.x.toDouble(),
              -100,
              100,
              titleStyle,
              valueStyle,
              (val) {
                setState(() => avatarData.mouth.x = val.toInt());
              },
              onDragEnd: _saveAvatarData,
            ),
            _buildSlider(
              "y",
              avatarData.mouth.y.toDouble(),
              -100,
              100,
              titleStyle,
              valueStyle,
              (val) {
                setState(() => avatarData.mouth.y = val.toInt());
              },
              onDragEnd: _saveAvatarData,
            ),
            _buildSlider(
              "rotation",
              avatarData.mouth.rotation.toDouble(),
              -1800,
              1800,
              titleStyle,
              valueStyle,
              (val) {
                setState(() => avatarData.mouth.rotation = val.toInt());
              },
              onDragEnd: _saveAvatarData,
            ),
            _buildSlider(
              "weight",
              avatarData.mouth.weight.toDouble(),
              0,
              100,
              titleStyle,
              valueStyle,
              (val) {
                setState(() => avatarData.mouth.weight = val.toInt());
              },
              onDragEnd: _saveAvatarData,
            ),
          ],
        ),
      ],
    );
  }

  Widget _buildMotionControls(BuildContext context) {
    TextStyle titleStyle = TextStyle(
      color: CupertinoColors.label.resolveFrom(context),
      fontSize: 15,
    );
    TextStyle valueStyle = TextStyle(
      color: CupertinoColors.secondaryLabel.resolveFrom(context),
      fontSize: 15,
    );

    return Column(
      mainAxisSize: MainAxisSize.min,
      children: [
        CupertinoListSection.insetGrouped(
          header: const Text("Joystick"),
          children: [
            SizedBox(
              height: 200,
              child: GridCoordinateJoystick(
                minX: -1280,
                maxX: 1280,
                minY: 0,
                maxY: 900,
                padding: const EdgeInsets.all(25),
                showMarking: false,
                targetGridSize: 50,
                buttonSize: 50,
                point: Offset(
                  motionData.yawServo.angle.toDouble(),
                  motionData.pitchServo.angle.toDouble(),
                ),
                onImmediatelyRelease: (point) {
                  setState(() {
                    motionData.yawServo.rotate = 0;
                    motionData.yawServo.angle = point.dx.toInt();
                    motionData.pitchServo.angle = point.dy.toInt();
                    _saveMotionData();
                  });
                },
              ),
            ),
          ],
        ),

        CupertinoListSection.insetGrouped(
          header: const Text("Yaw Servo"),
          children: [
            _buildSlider(
              "angle",
              motionData.yawServo.angle.toDouble(),
              -1280,
              1280,
              titleStyle,
              valueStyle,
              (val) {
                setState(() {
                  motionData.yawServo.rotate = 0;
                  motionData.yawServo.angle = val.toInt();
                });
              },
              onDragEnd: _saveMotionData,
            ),
            _buildSlider(
              "speed",
              motionData.yawServo.speed.toDouble(),
              0,
              1000,
              titleStyle,
              valueStyle,
              (val) {
                setState(() => motionData.yawServo.speed = val.toInt());
              },
              onDragEnd: _saveMotionData,
            ),
            _buildSlider(
              "rotate",
              motionData.yawServo.rotate.toDouble(),
              -1000,
              1000,
              titleStyle,
              valueStyle,
              (val) {
                setState(() {
                  motionData.yawServo.angle = 0;
                  motionData.yawServo.rotate = val.toInt();
                });
              },
              onDragEnd: _saveMotionData,
            ),
          ],
        ),

        CupertinoListSection.insetGrouped(
          header: const Text("Pitch Servo"),
          children: [
            _buildSlider(
              "angle",
              motionData.pitchServo.angle.toDouble(),
              0,
              900,
              titleStyle,
              valueStyle,
              (val) {
                setState(() => motionData.pitchServo.angle = val.toInt());
              },
              onDragEnd: _saveMotionData,
            ),
            _buildSlider(
              "speed",
              motionData.pitchServo.speed.toDouble(),
              0,
              1000,
              titleStyle,
              valueStyle,
              (val) {
                setState(() => motionData.pitchServo.speed = val.toInt());
              },
              onDragEnd: _saveMotionData,
            ),
          ],
        ),
      ],
    );
  }

  Widget _buildSlider(
    String label,
    double value,
    double min,
    double max,
    TextStyle titleStyle,
    TextStyle valueStyle,
    ValueChanged<double> onChanged, {
    VoidCallback? onDragEnd,
  }) {
    return Container(
      padding: const .symmetric(horizontal: 12),
      child: Row(
        children: [
          SizedBox(width: 60, child: Text(label, style: titleStyle)),
          Expanded(
            child: CupertinoSlider(
              value: value,
              onChanged: onChanged,
              max: max,
              min: min,
              onChangeEnd: (_) => onDragEnd?.call(),
            ),
          ),
          SizedBox(
            width: 50,
            child: Align(
              alignment: Alignment.centerRight,
              child: Text(value.toInt().toString(), style: valueStyle),
            ),
          ),
        ],
      ),
    );
  }
}
