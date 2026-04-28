/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

import 'dart:typed_data';

import 'package:flutter/cupertino.dart';
import 'package:stack_chan/app_state.dart';
import 'package:stack_chan/model/dance_list.dart';
import 'package:stack_chan/model/expression_data.dart';
import 'package:stack_chan/network/web_socket_util.dart';
import 'package:stack_chan/util/extension.dart';
import 'package:stack_chan/view/util/grid_coordinate_joystick.dart';
import 'package:stack_chan/view/util/stackchan_robot_box.dart';

class Motion extends StatefulWidget {
  const Motion({super.key});

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

    AppState.shared.sendWebSocketMessage(.getAvatarPosture);

    WebSocketUtil.shared.addObserver(tag, (message) {
      if (message is Uint8List) {
        final result = AppState.shared.parseMessage(message);
        final msgType = result.$1;
        final parsedData = result.$2;
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
  }

  void _saveAvatarData() {
    if (AppState.shared.deviceMac.isNotEmpty) {
      String jsonString = AppState.shared.deviceMac + avatarData.toString();
      AppState.shared.sendWebSocketMessage(
        .controlAvatar, //as WebSocketMessageType
        data: jsonString.toUint8List(),
      );
    }
  }

  void _saveMotionData() {
    if (AppState.shared.deviceMac.isNotEmpty) {
      String jsonString = AppState.shared.deviceMac + motionData.toString();
      AppState.shared.sendWebSocketMessage(
        .controlMotion,
        data: jsonString.toUint8List(),
      );
    }
  }

  @override
  Widget build(BuildContext context) {
    return ClipRSuperellipse(
      borderRadius: .only(topLeft: .circular(12), topRight: .circular(12)),
      child: CupertinoPageScaffold(
        backgroundColor: CupertinoColors.systemGroupedBackground.resolveFrom(
          context,
        ),
        navigationBar: CupertinoNavigationBar(
          trailing: CupertinoButton(
            padding: .zero,
            child: Icon(
              CupertinoIcons.xmark_circle_fill,
              size: 25,
              color: CupertinoColors.separator.resolveFrom(context),
            ),
            onPressed: () {
              CupertinoSheetRoute.popSheet(context);
            },
          ),
        ),
        child: ListView(
          padding: .only(
            top: MediaQuery.paddingOf(context).top + 15,
            left: 15,
            right: 15,
            bottom: MediaQuery.paddingOf(context).bottom + 15,
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
            Row(
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
                const SizedBox(width: 15),
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
              ],
            ),
            if (_selectedIndex == 1) _buildAvatarControls(context),
            if (_selectedIndex == 0) _buildMotionControls(context),
          ],
        ),
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
        _buildSectionTitle("Left Eye"),
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

        _buildSectionTitle("Right Eye"),
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

        _buildSectionTitle("Mouth"),
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

        SizedBox(height: MediaQuery.of(context).padding.bottom + 20),
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
        _buildSectionTitle("Joystick"),
        Container(
          height: 200,
          decoration: BoxDecoration(
            color: CupertinoColors.tertiarySystemBackground.resolveFrom(
              context,
            ),
            borderRadius: BorderRadius.circular(25),
          ),
          clipBehavior: Clip.antiAlias,
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

        _buildSectionTitle("Yaw Servo"),
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

        _buildSectionTitle("Pitch Servo"),
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
        SizedBox(height: MediaQuery.of(context).padding.bottom + 20),
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
    return Row(
      children: [
        SizedBox(width: 60, child: Text(label, style: titleStyle)),
        Expanded(
          child: CupertinoSlider(
            value: value,
            min: min,
            max: max,
            onChanged: onChanged,
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
    );
  }

  Widget _buildSectionTitle(String title) {
    return Padding(
      padding: const EdgeInsets.only(top: 16, bottom: 8),
      child: Align(
        alignment: Alignment.centerLeft,
        child: Text(
          title,
          style: CupertinoTheme.of(context).textTheme.navTitleTextStyle,
        ),
      ),
    );
  }
}
