/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

import 'dart:async';

import 'package:flex_color_picker/flex_color_picker.dart';
import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart';
import 'package:flutter_svg/flutter_svg.dart';
import 'package:get/get.dart' hide FormData, MultipartFile;
import 'package:intl/intl.dart';
import 'package:stack_chan/app_state.dart';
import 'package:stack_chan/model/dance_list.dart';
import 'package:stack_chan/model/expression_data.dart';
import 'package:stack_chan/util/blue_util.dart';
import 'package:stack_chan/util/extension.dart';
import 'package:stack_chan/util/music_util.dart';
import 'package:stack_chan/view/util/grid_coordinate_joystick.dart';
import 'package:uuid/uuid.dart';

import '../util/stackchan_robot_box.dart';
import 'add_music.dart';

class RecordDance extends StatefulWidget {
  const RecordDance({super.key, this.onResult});

  final Function(List<DanceData>, String, String?)? onResult;

  @override
  State<StatefulWidget> createState() => _RecordDanceState();
}

class RecordDanceModel extends GetxController {
  Rxn<MusicInfo> musicInfo = Rxn(null);
  RxnString musicUrl = RxnString(null);
  RxString danceName = RxString("");

  RxBool isPlaying = RxBool(false);
  RxBool isRecording = RxBool(false);
  RxDouble playbackProgress = RxDouble(0.0);

  Rx<ExpressionData> avatarData = Rx(
    ExpressionData(
      leftEye: ExpressionItem(weight: 100),
      rightEye: ExpressionItem(weight: 100),
      mouth: ExpressionItem(weight: 0),
    ),
  );
  Rx<MotionData> motionData = Rx(
    MotionData(pitchServo: MotionDataItem(), yawServo: MotionDataItem()),
  );

  Rx<String> leftRgbColor = RxString("#FFFFFF");
  Rx<String> rightRgbColor = RxString("#FFFFFF");

  RxList<double> bandFrequency = RxList([]);
}

class _RecordDanceState extends State<RecordDance> {
  RecordDanceModel model = RecordDanceModel();

  Timer? recordTimer;
  Timer? playbackTimer;
  DateTime? recordStartTime;
  final Uuid uuid = const Uuid();

  List<DanceData> recordedDanceFrames = [];

  @override
  void dispose() {
    stopAllTimers();
    MusicUtil.shared.stopMusic();
    model.onClose();
    super.dispose();
  }

  void stopAllTimers() {
    recordTimer?.cancel();
    playbackTimer?.cancel();
    recordTimer = null;
    playbackTimer = null;
  }

  String formatTime(int seconds) {
    final minutes = seconds ~/ 60;
    final remainingSeconds = seconds % 60;
    return DateFormat(
      'mm:ss',
    ).format(DateTime(0, 0, 0, 0, minutes, remainingSeconds));
  }

  void recordDanceFrame() {
    if (!model.isRecording.value) return;
    final danceFrame = DanceData(
      leftEye: model.avatarData.value.leftEye.copy(),
      rightEye: model.avatarData.value.rightEye.copy(),
      mouth: model.avatarData.value.mouth.copy(),
      yawServo: model.motionData.value.yawServo.copy(),
      pitchServo: model.motionData.value.pitchServo.copy(),
      leftRgbColor: model.leftRgbColor.value,
      rightRgbColor: model.rightRgbColor.value,
      durationMs: 100,
    );
    recordedDanceFrames.add(danceFrame);
      }

  Widget buildBandFrequencyChart(List<double> frequencies, double progress) {
    if (frequencies.isEmpty) {
      return const SizedBox(height: 0);
    }
    return SizedBox(
      height: 60,
      width: .infinity,
      child: Stack(
        clipBehavior: .none,
        alignment: .bottomCenter,
        children: [
          Row(
            crossAxisAlignment: .end,
            mainAxisAlignment: .spaceEvenly,
            children: frequencies.map((freq) {
              final normalizedFreq = freq.clamp(0.0, 1.0);
              return Expanded(
                child: Padding(
                  padding: const .symmetric(horizontal: 1),
                  child: Container(
                    height: normalizedFreq * 250,
                    decoration: BoxDecoration(
                      color: CupertinoColors.systemBlue.withValues(alpha: 0.7),
                      borderRadius: .vertical(top: .circular(2)),
                    ),
                  ),
                ),
              );
            }).toList(),
          ),
          Positioned(
            left: progress * MediaQuery.of(context).size.width - 1,
            top: 0,
            bottom: 0,
            child: Container(
              width: 2,
              color: CupertinoColors.systemRed,
              height: 60,
            ),
          ),
        ],
      ),
    );
  }

  @override
  Widget build(BuildContext context) {
    final theme = CupertinoTheme.of(context);
    List<Widget> listWidget = [];

    if (model.musicInfo.value != null) {
      listWidget.add(
        Column(
          mainAxisSize: .min,
          spacing: 8,
          children: [
            Row(
              children: [
                Obx(
                  () => Text(
                    model.musicInfo.value!.title ?? "Music",
                    style: theme.textTheme.textStyle,
                  ),
                ),
                Spacer(),
                CupertinoButton(
                  padding: .zero,
                  minimumSize: .zero,
                  child: Row(
                    spacing: 4,
                    mainAxisSize: .min,
                    children: [
                      Obx(
                        () => SvgPicture.asset(
                          model.isRecording.value
                              ? "assets/stop.circle.fill.svg"
                              : "assets/record.circle.fill.svg",
                          colorFilter: .mode(
                            model.isRecording.value
                                ? CupertinoColors.systemRed
                                : CupertinoColors.systemOrange,
                            .srcIn,
                          ),
                          width: 15,
                          height: 15,
                        ),
                      ),
                      Obx(
                        () => Text(
                          model.isRecording.value
                              ? "Stop Record"
                              : "Start Record",
                          style: theme.textTheme.textStyle,
                        ),
                      ),
                    ],
                  ),
                  onPressed: () {
                    if (model.isRecording.value) {
                      stopRecordingAndPlayback();
                    } else {
                      startRecordingAndPlayback();
                    }
                  },
                ),
              ],
            ),
            Obx(() {
              final musicInfo = model.musicInfo.value;
              final duration = musicInfo?.duration ?? 0;
              final currentSec = (model.playbackProgress.value * duration)
                  .toInt();

              return Column(
                mainAxisSize: .min,
                spacing: 4,
                children: [
                  Obx(
                    () => buildBandFrequencyChart(
                      model.bandFrequency,
                      model.playbackProgress.value,
                    ),
                  ),
                  LinearProgressIndicator(
                    value: model.playbackProgress.value,
                    color: theme.primaryColor,
                  ),
                  Row(
                    mainAxisAlignment: .spaceBetween,
                    children: [
                      Text(
                        formatTime(currentSec),
                        style: theme.textTheme.dateTimePickerTextStyle,
                      ),
                      Text(
                        formatTime(duration),
                        style: theme.textTheme.dateTimePickerTextStyle,
                      ),
                    ],
                  ),
                ],
              );
            }),
          ],
        ),
      );
    } else {
      listWidget.add(
        CupertinoButton(
          color: CupertinoColors.systemGroupedBackground.resolveFrom(context),
          borderRadius: .circular(12),
          padding: .zero,
          minimumSize: .zero,
          child: SizedBox(
            height: 60,
            child: Row(
              crossAxisAlignment: .center,
              children: [
                Spacer(),
                Text("Select Music", style: TextStyle(fontSize: 30)),
                Spacer(),
              ],
            ),
          ),
          onPressed: () {
            showCupertinoSheet(
              context: context,
              builder: (context) {
                return AddMusic(
                  onResult: (url) async {
                    final musicInfo = await MusicUtil.shared.getMusicInfoAsync(
                      url,
                    );
                    if (musicInfo != null) {
                      setState(() {
                        model.musicUrl.value = url;
                        model.musicInfo.value = musicInfo;
                        model.danceName.value = musicInfo.title ?? "";
                      });
                      final progressList = await model.musicInfo.value!
                          .getProgressData(targetSampleCount: 100);
                      model.bandFrequency.value = progressList;
                    }
                  },
                );
              },
            );
          },
        ),
      );
    }

    listWidget.add(
      Obx(
        () => StackChanRobotBox(
          topLook: true,
          width: double.infinity,
          height: 250,
          data: DanceData(
            leftEye: model.avatarData.value.leftEye,
            rightEye: model.avatarData.value.rightEye,
            mouth: model.avatarData.value.mouth,
            yawServo: model.motionData.value.yawServo,
            pitchServo: model.motionData.value.pitchServo,
            durationMs: 1000,
          ),
        ),
      ),
    );

    listWidget.add(
      Container(
        height: 200,
        width: .infinity,
        decoration: BoxDecoration(
          color: CupertinoColors.systemGroupedBackground.resolveFrom(context),
          borderRadius: BorderRadius.circular(25),
        ),
        child: Obx(
          () => GridCoordinateJoystick(
            minX: -1280,
            maxX: 1280,
            minY: 0,
            maxY: 900,
            padding: const EdgeInsets.all(25),
            showMarking: false,
            targetGridSize: 50,
            buttonSize: 50,
            point: Offset(
              model.motionData.value.yawServo.angle.toDouble(),
              model.motionData.value.pitchServo.angle.toDouble(),
            ),
            onImmediatelyRelease: (point) {
              model.motionData.value.yawServo.rotate = 0;
              model.motionData.value.yawServo.angle = point.dx.toInt();
              model.motionData.value.pitchServo.angle = point.dy.toInt();
              model.motionData.refresh();
              saveMotionData();
            },
          ),
        ),
      ),
    );

    listWidget.add(SizedBox(height: 15));

    listWidget.add(
      Row(
        children: [
          Text("Light strip left color", style: theme.textTheme.textStyle),
          Spacer(),
          Obx(
            () => CupertinoButton(
              borderRadius: .circular(50),
              color: CupertinoColors.systemGroupedBackground.resolveFrom(
                context,
              ),
              minimumSize: .zero,
              padding: .all(5),
              child: Container(
                width: 40,
                height: 40,
                decoration: BoxDecoration(
                  color: hexToColor(model.leftRgbColor.value),
                  borderRadius: .circular(50),
                ),
              ),
              onPressed: () {
                colorPickerDialog(true);
              },
            ),
          ),
        ],
      ),
    );

    listWidget.add(SizedBox(height: 15));

    listWidget.add(
      Row(
        children: [
          Text("Light strip right color", style: theme.textTheme.textStyle),
          Spacer(),
          Obx(
            () => CupertinoButton(
              borderRadius: .circular(50),
              color: CupertinoColors.systemGroupedBackground.resolveFrom(
                context,
              ),
              minimumSize: .zero,
              padding: .all(5),
              child: Container(
                width: 40,
                height: 40,
                decoration: BoxDecoration(
                  color: hexToColor(model.rightRgbColor.value),
                  borderRadius: .circular(50),
                ),
              ),
              onPressed: () {
                colorPickerDialog(false);
              },
            ),
          ),
        ],
      ),
    );

    return CupertinoPageScaffold(
      backgroundColor: CupertinoColors.systemBackground.resolveFrom(context),
      navigationBar: CupertinoNavigationBar.large(
        backgroundColor: CupertinoColors.systemBackground.resolveFrom(context),
        largeTitle: Text("Record Dance"),
        trailing: CupertinoButton(
          sizeStyle: .medium,
          onPressed: () async {
            if (model.isRecording.value) {
              stopRecordingAndPlayback();
            }
            CupertinoSheetRoute.popSheet(context);
            //directreturn
            widget.onResult?.call(
              recordedDanceFrames,
              model.musicUrl.value ?? "",
              model.danceName.value,
            );
          },
          child: Icon(CupertinoIcons.check_mark),
        ),
        leading: CupertinoButton(
          sizeStyle: .medium,
          child: Icon(CupertinoIcons.xmark),
          onPressed: () => CupertinoSheetRoute.popSheet(context),
        ),
      ),
      child: ListView(padding: .all(15), children: listWidget),
    );
  }

  Future<bool> colorPickerDialog(bool isLeft) async {
    return ColorPicker(
      // Use the dialogPickerColor as start and active color.
      color: isLeft
          ? hexToColor(model.leftRgbColor.value)
          : hexToColor(model.rightRgbColor.value),
      // Update the dialogPickerColor using the callback.
      onColorChanged: (Color color) {
        if (isLeft) {
          model.leftRgbColor.value = colorToHex(color);
        } else {
          model.rightRgbColor.value = colorToHex(color);
        }
      },
      width: 40,
      height: 40,
      borderRadius: 4,
      spacing: 5,
      runSpacing: 5,
      wheelDiameter: 155,
      heading: Text(
        'Select color',
        style: CupertinoTheme.of(context).textTheme.navTitleTextStyle,
      ),
      subheading: Text(
        'Select color shade',
        style: CupertinoTheme.of(context).textTheme.navTitleTextStyle,
      ),
      wheelSubheading: Text(
        'Selected color and its shades',
        style: CupertinoTheme.of(context).textTheme.navTitleTextStyle,
      ),
      showMaterialName: true,
      showColorName: true,
      showColorCode: true,
      copyPasteBehavior: const ColorPickerCopyPasteBehavior(
        longPressMenu: true,
      ),

      pickersEnabled: const <ColorPickerType, bool>{
        ColorPickerType.both: false,
        ColorPickerType.primary: true,
        ColorPickerType.accent: true,
        ColorPickerType.bw: false,
        ColorPickerType.custom: true,
        ColorPickerType.wheel: true,
      },
    ).showPickerDialog(context);
  }

  String colorToHex(Color color) {
    return '#${color.toARGB32().toRadixString(16).padLeft(8, '0').substring(2).toUpperCase()}';
  }

  Color hexToColor(String hexString) {
    final hex = hexString.replaceAll('#', '');
    return Color(int.parse('FF$hex', radix: 16));
  }

  void stopRecordingAndPlayback() {
    MusicUtil.shared.stopMusic();

    stopAllTimers();

    model.isRecording.value = false;
    model.isPlaying.value = false;

    if (model.playbackProgress.value > 0.9) {
      model.playbackProgress.value = 1.0;
    }
  }

  void startRecordingAndPlayback() {
    final musicInfo = model.musicInfo.value;
    if (musicInfo == null) return;

    recordedDanceFrames.clear();
    model.playbackProgress.value = 0.0;
    model.isRecording.value = true;
    recordStartTime = DateTime.now();
    recordTimer = Timer.periodic(const Duration(milliseconds: 100), (timer) {
      recordDanceFrame();
    });
    MusicUtil.shared.playMusicOnce(musicInfo, () {
      stopRecordingAndPlayback();
    });

    playbackTimer = Timer.periodic(const Duration(milliseconds: 50), (timer) {
      final duration = MusicUtil.shared.getMusicDuration();
      final currentPosition = MusicUtil.shared.getCurrentPosition();

      if (duration > 0 && currentPosition >= 0) {
        //calculatenormalizeprogress (0.0 to 1.0)
        final progress = currentPosition / duration;
        model.playbackProgress.value = progress.clamp(0.0, 1.0);
      }
    });
  }

  DateTime lastBluetoothSendTime = DateTime.now();

  void saveMotionData() {
    if (AppState.shared.deviceControlMode == 0) {
      if (AppState.shared.deviceMac.isNotEmpty) {
        final jsonString =
            AppState.shared.deviceMac + model.motionData.value.toString();
        final data = jsonString.toUint8List();
        AppState.shared.sendWebSocketMessage(.controlMotion, data: data);
      }
    } else {
      final currentTime = DateTime.now();
      final timeInterval = currentTime
          .difference(lastBluetoothSendTime)
          .inMilliseconds;
      if (timeInterval >= 200) {
        final danceData = DanceData(
          leftEye: ExpressionItem(weight: 100),
          rightEye: ExpressionItem(weight: 100),
          mouth: ExpressionItem(weight: 0),
          yawServo: model.motionData.value.yawServo,
          pitchServo: model.motionData.value.pitchServo,
          durationMs: 0,
        );
        BlueUtil.shared.sendDanceData(danceData);
        lastBluetoothSendTime = currentTime;
      }
    }
  }
}
