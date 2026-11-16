/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

import 'dart:async';
import 'dart:convert';

import 'package:flex_color_picker/flex_color_picker.dart';
import 'package:cupertino_ui/cupertino_ui.dart';
import 'package:stack_chan/view/util/sliver_cupertino_list_section.dart';
import 'package:stack_chan/view/util/theme_setting.dart';
import 'package:flutter_svg/flutter_svg.dart';
import 'package:get/get.dart';
import 'package:stack_chan/app_state.dart';
import 'package:stack_chan/model/dance_list.dart';
import 'package:stack_chan/model/expression_data.dart';
import 'package:stack_chan/model/model.dart';
import 'package:stack_chan/network/http.dart';
import 'package:stack_chan/network/urls.dart';
import 'package:stack_chan/util/blue_util.dart';
import 'package:stack_chan/util/music_util.dart';
import 'package:stack_chan/util/value_constant.dart';
import 'package:stack_chan/view/util/grid_coordinate_joystick.dart';

import '../../util/extension.dart';

class Dance extends StatefulWidget {
  const Dance({super.key, required this.danceInfo});

  final DanceList danceInfo;

  @override
  State<StatefulWidget> createState() => _DanceState();
}

class DanceModel extends GetxController {
  RxInt selectedDance = RxInt(0); // Index of the selected dance frame
  Rx<DanceList> danceInfo = Rx(DanceList()); // Dance data currently being edited
  RxInt dancePlayIndex = RxInt(-1); // Index of the frame being played (for highlighting)
  RxBool isRun = RxBool(false); // Whether the whole dance is playing
  RxBool isLoop = RxBool(false); // Loop playback toggle

  // Playback control
  Timer? playTimer; // Timer for whole-dance playback (network mode)
  List<Future<void>?> bluetoothPlayTasks = []; // Pending Bluetooth playback tasks
  RxBool isPlayingSingle = RxBool(false); // Whether a single frame is playing
}

class _DanceState extends State<Dance> {
  late DanceModel model;

  @override
  void initState() {
    super.initState();
    model = DanceModel();
    model.danceInfo.value = widget.danceInfo;
  }

  @override
  void dispose() {
    model.isRun.value = false;
    model.isLoop.value = false; // Reset loop state when disposed
    model.onClose();
    stopDance(); // Force playback to stop when disposed
    super.dispose();
  }

  /// Saves the current dance data to the backend.
  void saveDance() async {
    Map<String, dynamic> map = {
      ValueConstant.id: model.danceInfo.value.id,
      ValueConstant.danceData: model.danceInfo.value.danceDataToJson(),
      ValueConstant.musicUrl: model.danceInfo.value.musicUrl ?? "",
      ValueConstant.danceName: model.danceInfo.value.danceName ?? "Dance",
    };
    final response = await Http.instance.put(Urls.v2dance, data: map);
    if (response.data != null) {
      Model modelRes = Model.fromJsonT(response.data);
      if (!modelRes.isSuccess()) {
        AppState.shared.showToast(modelRes.message);
      }
    }
  }

  /// Refreshes the dance data from the backend.
  Future<void> getDanceList() async {
    final Map<String, dynamic> map = {
      ValueConstant.id: model.danceInfo.value.id,
    };

    final response = await Http.instance.get(Urls.dance, data: map);
    if (response.data != null) {
      Model<DanceList> responseData = Model.fromJsonT(
        response.data,
        factory: (data) => DanceList.fromJson(data),
      );
      if (responseData.isSuccess() && responseData.data != null) {
        final data = responseData.data!;
        model.danceInfo.value = data;
      }
    }
  }

  /// Plays the selected frame once.
  void startDanceOne() {
    // Stop any whole-dance playback in progress
    if (model.isRun.value) {
      stopDance();
      model.isRun.value = false;
    }
    // Single-frame playback already stopped everything; nothing more to do
    if (model.isPlayingSingle.value) return;

    final danceDataList = model.danceInfo.value.danceData;
    if (danceDataList.isEmpty) {
      Get.snackbar("提示", "暂无舞蹈数据可播放", snackPosition: SnackPosition.BOTTOM);
      return;
    }

    // Clamp the selected index to the valid range
    final int selectedIndex = model.selectedDance.value.clamp(
      0,
      danceDataList.length - 1,
    );
    final DanceData currentData = danceDataList[selectedIndex];

    // Update the playback state
    model.isPlayingSingle.value = true;
    model.dancePlayIndex.value = selectedIndex; // Highlight the frame being played

    /// Core single-frame playback logic.
    Future<void> playSingleFrame() async {
      try {
        if (AppState.shared.deviceControlMode == 0) {
          // Network mode: send the frame as JSON
          final jsonString = jsonEncode([currentData.toJson()]);
          AppState.shared.sendWebSocketMessage(
            .dance,
            data: jsonString.toUint8List(),
          );
          // Wait for the frame duration
          await Future.delayed(Duration(milliseconds: currentData.durationMs));
        } else if (AppState.shared.deviceControlMode == 1) {
          // Bluetooth mode: send the frame directly to the device
          await BlueUtil.shared.sendDanceData(currentData);
          // Extra delay to stay in sync with the device's response
          await Future.delayed(
            Duration(milliseconds: currentData.durationMs + 70),
          );
        }
      } catch (e) {
        debugPrint("Single framePlayFailed: $e");
        Get.snackbar("错误", "播放失败: $e", snackPosition: SnackPosition.BOTTOM);
      } finally {
        // Reset the state once playback completes
        model.isPlayingSingle.value = false;
        model.dancePlayIndex.value = -1;
      }
    }

    playSingleFrame();
  }

  /// Plays the whole dance, optionally looping.
  void startDance() {
    // Stop anything currently playing
    stopDance();

    final danceDataList = model.danceInfo.value.danceData;
    if (danceDataList.isEmpty) {
      Get.snackbar("提示", "暂无舞蹈数据可播放", snackPosition: SnackPosition.BOTTOM);
      model.isRun.value = false;
      return;
    }

    // Start the associated music, if any
    if (model.danceInfo.value.musicUrl != null &&
        model.danceInfo.value.musicUrl!.isNotEmpty) {
      MusicUtil.shared.stopMusic(); // Stop any music that is already playing
      MusicUtil.shared.playMusic(
        model.danceInfo.value.musicInfo,
        isLoop: model.isLoop.value,
      ); // Keep the music in sync with the loop setting
    }

    // Wrap the playback logic so it can call itself when looping
    void playFullDance() {
      if (!model.isRun.value) return; // Exit if playback was stopped

      if (AppState.shared.deviceControlMode == 0) {
        // Network mode: send all dance data in one message
        final jsonString = jsonEncode(DanceData.listToJson(danceDataList));
        AppState.shared.sendWebSocketMessage(
          .dance,
          data: jsonString.toUint8List(),
        );

        // Record the start time to track elapsed playback time accurately
        final startTime = DateTime.now();

        // Timer that advances the frame index for UI highlighting
        model.playTimer = Timer.periodic(const Duration(milliseconds: 50), (
          timer,
        ) {
          if (!model.isRun.value) {
            timer.cancel();
            return;
          }

          // Compute elapsed time precisely to avoid timer drift
          final elapsedMs = DateTime.now().difference(startTime).inMilliseconds;

          // Determine the frame that should be playing now
          int currentIndex = -1;
          int accumulatedDuration = 0;
          for (int i = 0; i < danceDataList.length; i++) {
            final frameDuration = danceDataList[i].durationMs;
            if (elapsedMs < accumulatedDuration + frameDuration) {
              currentIndex = i;
              break;
            }
            accumulatedDuration += frameDuration;
          }

          if (currentIndex != -1) {
            model.dancePlayIndex.value = currentIndex;
          } else {
            // Playback finished
            model.dancePlayIndex.value = -1;
            timer.cancel();

            // Restart when looping is enabled
            if (model.isLoop.value && model.isRun.value) {
              // Restart the music and keep it in sync
              if (model.danceInfo.value.musicUrl != null &&
                  model.danceInfo.value.musicUrl!.isNotEmpty) {
                MusicUtil.shared.stopMusic();
                MusicUtil.shared.playMusic(
                  model.danceInfo.value.musicInfo,
                  isLoop: true,
                );
              }
              Future.delayed(const Duration(milliseconds: 100), () {
                if (model.isRun.value) playFullDance();
              });
            } else {
              stopDance();
              model.isRun.value = false;
            }
          }
        });
      } else if (AppState.shared.deviceControlMode == 1) {
        // Bluetooth mode: send frames one by one, looping when enabled
        Future<void> playAllFrames() async {
          for (int i = 0; i < danceDataList.length; i++) {
            // Stop if playback was cancelled
            if (!model.isRun.value) break;

            final DanceData currentData = danceDataList[i];
            model.dancePlayIndex.value = i; // Highlight the current frame

            try {
              // Send the frame over Bluetooth
              await BlueUtil.shared.sendDanceData(currentData);
              // Wait for the frame duration plus device response time
              await Future.delayed(
                Duration(milliseconds: currentData.durationMs + 70),
              );
            } catch (e) {
              debugPrint("BluetoothPlayFrame $i Failed: $e");
              break;
            }
          }

          // Handle completion of playback
          model.dancePlayIndex.value = -1;
          if (model.isRun.value) {
            // Replay when loop mode is on
            if (model.isLoop.value) {
              // Restart the music and keep it in sync
              if (model.danceInfo.value.musicUrl != null &&
                  model.danceInfo.value.musicUrl!.isNotEmpty) {
                MusicUtil.shared.stopMusic();
                MusicUtil.shared.playMusic(
                  model.danceInfo.value.musicInfo,
                  isLoop: true,
                );
              }
              Future.delayed(const Duration(milliseconds: 100), () {
                if (model.isRun.value) playAllFrames();
              });
            } else {
              stopDance();
              model.isRun.value = false;
            }
          }
        }

        // Track the task so it can be cancelled
        final task = playAllFrames();
        model.bluetoothPlayTasks.add(task);
        task.whenComplete(() => model.bluetoothPlayTasks.remove(task));
      }
    }

    // Start the first pass
    playFullDance();
  }

  /// Stops all playback.
  void stopDance() {
    // Stop the music
    MusicUtil.shared.stopMusic();

    unawaited(_sendOriginDanceData());

    // Reset the playback state
    model.isPlayingSingle.value = false;
    model.dancePlayIndex.value = -1;

    // Cancel the playback timer
    model.playTimer?.cancel();
    model.playTimer = null;

    // Cancel all pending Bluetooth playback tasks
    for (var task in model.bluetoothPlayTasks) {
      task?.ignore();
    }
    model.bluetoothPlayTasks.clear();
  }

  Future<void> _sendOriginDanceData() async {
    final originData = DanceData.init();
    try {
      if (AppState.shared.deviceControlMode == 0) {
        final jsonString = jsonEncode([originData.toJson()]);
        AppState.shared.sendWebSocketMessage(
          .dance,
          data: jsonString.toUint8List(),
        );
      } else if (AppState.shared.deviceControlMode == 1) {
        await BlueUtil.shared.sendDanceData(originData);
      }
    } catch (e) {
      debugPrint("send origin dance data failed: $e");
    }
  }

  @override
  Widget build(BuildContext context) {
    final screenPadding = ThemeSetting.screenPadding(context);
    return CupertinoPageScaffold(
      backgroundColor: CupertinoColors.systemGroupedBackground.resolveFrom(
        context,
      ),
      child: CustomScrollView(
        slivers: [
          CupertinoSliverNavigationBar(
            backgroundColor: ThemeSetting.cupertinoNavigationBarBackgroundColor(
              context,
            ),
            border: ThemeSetting.cupertinoNavigationBarBorder(context),
            padding: ThemeSetting.navigationBarPadding,
            largeTitle: Obx(
              () => Text(model.danceInfo.value.danceName ?? "Dance"),
            ),
            trailing: Row(
              mainAxisSize: MainAxisSize.min,
              children: [
                Obx(
                  () => CupertinoButton(
                    sizeStyle: .medium,
                    child: SvgPicture.asset(
                      model.isLoop.value
                          ? "assets/repeat.fill.svg" // Loop enabled icon
                          : "assets/repeat.svg", // Loop disabled icon
                      colorFilter: ColorFilter.mode(
                        model.isLoop.value
                            ? CupertinoTheme.of(context).primaryColor
                            : CupertinoColors.systemGrey,
                        BlendMode.srcIn,
                      ),
                    ),
                    onPressed: () {
                      // Toggle loop mode
                      model.isLoop.value = !model.isLoop.value;
                      // Update the music loop setting if playback is active
                      if (model.isRun.value &&
                          model.danceInfo.value.musicUrl?.isNotEmpty == true) {
                        MusicUtil.shared.setMusicLoop(model.isLoop.value);
                      }
                    },
                  ),
                ),
                // Play/stop button for the whole dance
                CupertinoButton(
                  sizeStyle: .medium,
                  child: Obx(
                    () => SvgPicture.asset(
                      model.isRun.value
                          ? "assets/stop.fill.svg"
                          : "assets/play.fill.svg",
                      colorFilter: ColorFilter.mode(
                        CupertinoTheme.of(context).primaryColor,
                        BlendMode.srcIn,
                      ),
                    ),
                  ),
                  onPressed: () {
                    model.isRun.value = !model.isRun.value;
                    if (model.isRun.value) {
                      startDance();
                    } else {
                      stopDance();
                    }
                  },
                ),
              ],
            ),
          ),
          // Pull to refresh
          CupertinoSliverRefreshControl(onRefresh: getDanceList),
          // Dance list
          SliverPadding(
            padding: .only(
              left: screenPadding.left,
              right: screenPadding.right,
              bottom: screenPadding.bottom + 10,
            ),
            sliver: Obx(() {
              if (model.danceInfo.value.danceData.isEmpty) {
                return SliverToBoxAdapter(
                  child: CupertinoListSection.insetGrouped(
                    children: [
                      CupertinoListTile(
                        onTap: () {
                          model.danceInfo.value.danceData.add(
                            DanceData(
                              leftEye: ExpressionItem(weight: 100),
                              rightEye: ExpressionItem(weight: 100),
                              mouth: ExpressionItem(weight: 0),
                              yawServo: MotionDataItem(),
                              pitchServo: MotionDataItem(),
                              durationMs: 200,
                            ),
                          );
                          model.danceInfo.refresh();
                          saveDance();
                        },
                        title: const Center(
                          child: Icon(CupertinoIcons.add_circled),
                        ),
                      ),
                    ],
                  ),
                );
              } else {
                return SliverCupertinoListSectionInsetGrouped(
                  decoration: BoxDecoration(
                    color: CupertinoColors.secondarySystemGroupedBackground
                        .resolveFrom(context),
                    borderRadius: .circular(ThemeSetting.cardRadius),
                  ),
                  itemBuilder: (context, index) {
                    return danceItemView(context, index);
                  },
                  itemCount: model.danceInfo.value.danceData.length,
                );
              }
            }),
          ),
        ],
      ),
    );
  }

  /// A single dance frame row; logic kept unchanged.
  Widget danceItemView(BuildContext context, int index) {
    TextStyle titleStyle = TextStyle(
      color: CupertinoColors.label.resolveFrom(context),
      fontSize: 15,
    );
    TextStyle valueStyle = TextStyle(
      color: CupertinoColors.secondaryLabel.resolveFrom(context),
      fontSize: 15,
      fontWeight: FontWeight.w500,
    );

    return Obx(
      () => Container(
        color: index == model.dancePlayIndex.value
            ? CupertinoColors.systemPink
                  .resolveFrom(context)
                  .withValues(alpha: 0.2)
            : CupertinoColors.transparent,
        child: CupertinoExpansionTile(
          transitionMode: ExpansionTileTransitionMode.scroll,
          title: Row(
            children: [
              SizedBox(
                width: 80,
                child: Row(
                  mainAxisSize: .max,
                  children: [
                    const Spacer(),
                    CupertinoButton(
                      sizeStyle: .small,
                      padding: EdgeInsets.zero,
                      child: Icon(
                        CupertinoIcons.minus_circle,
                        color: CupertinoColors.separator.resolveFrom(context),
                      ),
                      onPressed: () {
                        model.danceInfo.value.danceData.removeAt(index);
                        model.danceInfo.refresh();
                        saveDance();
                      },
                    ),
                    const Spacer(),
                    CupertinoButton(
                      padding: EdgeInsets.zero,
                      sizeStyle: .small,
                      onPressed: () {
                        // Duplicate the current frame
                        final currentData = model
                            .danceInfo
                            .value
                            .danceData[index]
                            .copy();
                        if (index + 1 <
                            model.danceInfo.value.danceData.length) {
                          model.danceInfo.value.danceData.insert(
                            index,
                            currentData,
                          );
                        } else {
                          model.danceInfo.value.danceData.add(currentData);
                        }
                        model.danceInfo.refresh();
                        saveDance();
                      },
                      child: const Icon(CupertinoIcons.plus_circle),
                    ),
                    const Spacer(),
                  ],
                ),
              ),
              Text(
                "Dance ${index + 1}",
                style: TextStyle(
                  color: CupertinoColors.label.resolveFrom(context),
                  fontWeight: FontWeight.w700,
                ),
              ),
              const Spacer(),
              Text(
                "${model.danceInfo.value.danceData[index].durationMs} ms",
                style: TextStyle(
                  color: CupertinoColors.secondaryLabel.resolveFrom(context),
                  fontWeight: FontWeight.w700,
                ),
              ),
              const SizedBox(width: 10),
            ],
          ),
          child: Padding(
            padding: const EdgeInsets.all(12),
            child: Row(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                const SizedBox(width: 80),
                Expanded(
                  child: Column(
                    mainAxisSize: MainAxisSize.min,
                    children: [
                      // Orientation angle readout
                      Row(
                        children: [
                          Text("Orientation", style: titleStyle),
                          const Spacer(),
                          Obx(
                            () => Text(
                              "x: ${model.danceInfo.value.danceData[index].yawServo.angle}  y: ${model.danceInfo.value.danceData[index].pitchServo.angle}",
                              style: valueStyle,
                            ),
                          ),
                        ],
                      ),
                      // Orientation joystick
                      LayoutBuilder(
                        builder: (context, constraints) {
                          return Container(
                            height: constraints.maxWidth / 2,
                            decoration: BoxDecoration(
                              color: CupertinoColors
                                  .tertiarySystemGroupedBackground
                                  .resolveFrom(context),
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
                                  model
                                      .danceInfo
                                      .value
                                      .danceData[index]
                                      .yawServo
                                      .angle
                                      .toDouble(),
                                  model
                                      .danceInfo
                                      .value
                                      .danceData[index]
                                      .pitchServo
                                      .angle
                                      .toDouble(),
                                ),
                                onImmediatelyRelease: (point) {
                                  setState(() {
                                    model
                                            .danceInfo
                                            .value
                                            .danceData[index]
                                            .yawServo
                                            .rotate =
                                        0;
                                    model
                                        .danceInfo
                                        .value
                                        .danceData[index]
                                        .yawServo
                                        .angle = point.dx
                                        .toInt();
                                    model
                                        .danceInfo
                                        .value
                                        .danceData[index]
                                        .pitchServo
                                        .angle = point.dy
                                        .toInt();
                                    saveDance();
                                  });
                                },
                              ),
                            ),
                          );
                        },
                      ),
                      const SizedBox(height: 10),

                      // Left light strip color picker
                      Row(
                        children: [
                          Text("Light strip left color", style: titleStyle),
                          const Spacer(),
                          CupertinoButton(
                            padding: const EdgeInsets.all(5),
                            child: Obx(
                              () => Container(
                                width: 40,
                                height: 40,
                                decoration: BoxDecoration(
                                  color: hexToColor(
                                    model
                                        .danceInfo
                                        .value
                                        .danceData[index]
                                        .leftRgbColor,
                                  ),
                                  borderRadius: BorderRadius.circular(50),
                                ),
                              ),
                            ),
                            onPressed: () => colorPickerDialog(
                              true,
                              model.danceInfo.value.danceData[index],
                            ),
                          ),
                        ],
                      ),

                      // Right light strip color picker
                      const SizedBox(height: 10),

                      Row(
                        children: [
                          Text("Light strip right color", style: titleStyle),
                          const Spacer(),
                          CupertinoButton(
                            padding: const EdgeInsets.all(5),
                            child: Obx(
                              () => Container(
                                width: 40,
                                height: 40,
                                decoration: BoxDecoration(
                                  color: hexToColor(
                                    model
                                        .danceInfo
                                        .value
                                        .danceData[index]
                                        .rightRgbColor,
                                  ),
                                  borderRadius: BorderRadius.circular(50),
                                ),
                              ),
                            ),
                            onPressed: () => colorPickerDialog(
                              false,
                              model.danceInfo.value.danceData[index],
                            ),
                          ),
                        ],
                      ),

                      const SizedBox(height: 10),
                      Row(
                        children: [
                          Text("Exercise duration", style: titleStyle),
                          const Spacer(),
                          Obx(
                            () => Text(
                              "ms: ${model.danceInfo.value.danceData[index].durationMs}",
                              style: valueStyle,
                            ),
                          ),
                        ],
                      ),
                      const SizedBox(height: 10),
                      // Duration slider
                      SizedBox(
                        width: double.infinity,
                        child: CupertinoSlider(
                          max: 3000,
                          min: 0,
                          value: model
                              .danceInfo
                              .value
                              .danceData[index]
                              .durationMs
                              .toDouble(),
                          onChanged: (value) {
                            setState(() {
                              model
                                  .danceInfo
                                  .value
                                  .danceData[index]
                                  .durationMs = value
                                  .toInt();
                            });
                          },
                          onChangeEnd: (value) => saveDance(),
                        ),
                      ),
                    ],
                  ),
                ),
              ],
            ),
          ),
        ),
      ),
    );
  }

  String colorToHex(Color color) {
    return '#${color.toARGB32().toRadixString(16).padLeft(8, '0').substring(2).toUpperCase()}';
  }

  Color hexToColor(String hexString) {
    final hex = hexString.replaceAll('#', '');
    return Color(int.parse('FF$hex', radix: 16));
  }

  // Color picker popup; logic kept unchanged.
  Future<bool> colorPickerDialog(bool isLeft, DanceData danceData) async {
    Color initialColor = isLeft
        ? hexToColor(danceData.leftRgbColor)
        : hexToColor(danceData.rightRgbColor);

    CupertinoSlidingSegmentedControl;

    return ColorPicker(
          color: initialColor,
          onColorChanged: (Color color) {
            if (isLeft) {
              danceData.leftRgbColor = colorToHex(color);
            } else {
              danceData.rightRgbColor = colorToHex(color);
            }
            model.danceInfo.refresh();
          },
          enableOpacity: false,
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
        )
        .showPickerDialog(
          context,
          backgroundColor: CupertinoColors.systemGroupedBackground,
        )
        .then((value) {
          if (value == true) {
            saveDance(); // Persist the selected color
          }
          return value;
        });
  }
}
