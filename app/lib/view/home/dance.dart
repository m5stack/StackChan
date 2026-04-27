/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

import 'dart:async';
import 'dart:convert';

import 'package:flex_color_picker/flex_color_picker.dart';
import 'package:flutter/cupertino.dart';
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
  RxInt selectedDance = RxInt(0); //currentSelectedDance帧index
  Rx<DanceList> danceInfo = Rx(DanceList()); //danceData
  RxInt dancePlayIndex = RxInt(-1); //正inplay帧index（forhighlight）
  RxBool isRun = RxBool(false); //whether正inwholeplay
  RxBool isLoop = RxBool(false); //new增：loopplaymode开关

  //playControl related
  Timer? playTimer; //wholeplaytimer（网络mode）
  List<Future<void>?> bluetoothPlayTasks = []; //Bluetoothplaytasklist
  RxBool isPlayingSingle = RxBool(false); //whether正inplay单帧
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
    model.isLoop.value = false; //disposewhenresetloopstate
    model.onClose();
    stopDance(); //disposewhen强制stopplay
    super.dispose();
  }

  ///savedancedatatoserviceSide / End
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

  ///fromserviceSide / Endrefreshdancedata
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

  ///playselectedSingle framedance
  void startDanceOne() {
    //stopCurrentlyinperformwholeplay
    if (model.isRun.value) {
      stopDance();
      model.isRun.value = false;
    }
    //Single frameplayinThendirectreturn
    if (model.isPlayingSingle.value) return;

    final danceDataList = model.danceInfo.value.danceData;
    if (danceDataList.isEmpty) {
      Get.snackbar("提示", "暂无舞蹈数据可播放", snackPosition: SnackPosition.BOTTOM);
      return;
    }

    //verifyselectedindexhasEffectiveness
    final int selectedIndex = model.selectedDance.value.clamp(
      0,
      danceDataList.length - 1,
    );
    final DanceData currentData = danceDataList[selectedIndex];

    //updateplaystate
    model.isPlayingSingle.value = true;
    model.dancePlayIndex.value = selectedIndex; //highlightcurrentplay帧

    ///Single frameplaycorelogic
    Future<void> playSingleFrame() async {
      try {
        if (AppState.shared.deviceControlMode == 0) {
          //NetworkControlmode:sendSingle frameJSONdata
          final jsonString = jsonEncode([currentData.toJson()]);
          AppState.shared.sendWebSocketMessage(
            .dance,
            data: jsonString.toUint8List(),
          );
          //Waitcurrent frameplayduration
          await Future.delayed(Duration(milliseconds: currentData.durationMs));
        } else if (AppState.shared.deviceControlMode == 1) {
          //BluetoothControlmode:directsendFramedatatoBluetoothdevice
          await BlueUtil.shared.sendDanceData(currentData);
          //Delay(+70ms forAligneddeviceresponselogic)
          await Future.delayed(
            Duration(milliseconds: currentData.durationMs + 70),
          );
        }
      } catch (e) {
        debugPrint("Single framePlayFailed: $e");
        Get.snackbar("错误", "播放失败: $e", snackPosition: SnackPosition.BOTTOM);
      } finally {
        //playcompleteafterresetstate
        model.isPlayingSingle.value = false;
        model.dancePlayIndex.value = -1;
      }
    }

    playSingleFrame();
  }

  ///playwholedance（supportloop）
  void startDance() {
    //stopallCurrentlyinperformplay
    stopDance();

    final danceDataList = model.danceInfo.value.danceData;
    if (danceDataList.isEmpty) {
      Get.snackbar("提示", "暂无舞蹈数据可播放", snackPosition: SnackPosition.BOTTOM);
      model.isRun.value = false;
      return;
    }

    //playAssociatedmusic(ifhas)
    if (model.danceInfo.value.musicUrl != null &&
        model.danceInfo.value.musicUrl!.isNotEmpty) {
      MusicUtil.shared.stopMusic(); //stoporiginalhasmusic
      MusicUtil.shared.playMusic(
        model.danceInfo.value.musicInfo,
        isLoop: model.isLoop.value,
      ); //musicsyncloop
    }

    //wrapwholeplaylogic(forloopCall)
    void playFullDance() {
      if (!model.isRun.value) return; //已stop则exit

      if (AppState.shared.deviceControlMode == 0) {
        //NetworkControlmode:1One-timesendalldancedata
        final jsonString = jsonEncode(DanceData.listToJson(danceDataList));
        AppState.shared.sendWebSocketMessage(
          .dance,
          data: jsonString.toUint8List(),
        );

        //Recordplaystarttime,forPrecise / AccuratelycalculateAlreadyplayduration
        final startTime = DateTime.now();

        //timerupdateplayFrameindex(forUIhighlight)
        model.playTimer = Timer.periodic(const Duration(milliseconds: 50), (
          timer,
        ) {
          if (!model.isRun.value) {
            timer.cancel();
            return;
          }

          //Precise / AccuratelycalculateAlreadyplayduration(avoidtimerDeviation)
          final elapsedMs = DateTime.now().difference(startTime).inMilliseconds;

          //calculatecurrentplaytoFrame
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
            //playcomplete
            model.dancePlayIndex.value = -1;
            timer.cancel();

            //ifenableloop,Thenreplay
            if (model.isLoop.value && model.isRun.value) {
              //replaymusic,Maintain / Keepsync
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
        //BluetoothControlmode:frame by framesenddata(supportloop)
        Future<void> playAllFrames() async {
          for (int i = 0; i < danceDataList.length; i++) {
            //checkwhetherNeedstopplay
            if (!model.isRun.value) break;

            final DanceData currentData = danceDataList[i];
            model.dancePlayIndex.value = i; //highlightcurrent frame

            try {
              //sendBluetoothdata
              await BlueUtil.shared.sendDanceData(currentData);
              //WaitFrameduration(+70ms forAligneddeviceresponse)
              await Future.delayed(
                Duration(milliseconds: currentData.durationMs + 70),
              );
            } catch (e) {
              debugPrint("BluetoothPlayFrame $i Failed: $e");
              break;
            }
          }

          //playcompleteafterhandle
          model.dancePlayIndex.value = -1;
          if (model.isRun.value) {
            //loopmodeThenreplay
            if (model.isLoop.value) {
              //replaymusic,Maintain / Keepsync
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

        //willtaskaddlist(For easycancel)
        final task = playAllFrames();
        model.bluetoothPlayTasks.add(task);
        task.whenComplete(() => model.bluetoothPlayTasks.remove(task));
      }
    }

    //startfirstplay
    playFullDance();
  }

  ///stopallplay
  void stopDance() {
    //stopmusic
    MusicUtil.shared.stopMusic();

    //resetplaystate
    model.isPlayingSingle.value = false;
    model.dancePlayIndex.value = -1;

    //canceltimer
    model.playTimer?.cancel();
    model.playTimer = null;

    //cancelallBluetoothplaytask
    for (var task in model.bluetoothPlayTasks) {
      task?.ignore();
    }
    model.bluetoothPlayTasks.clear();
  }

  @override
  Widget build(BuildContext context) {
    return CupertinoPageScaffold(
      backgroundColor: CupertinoColors.systemGroupedBackground.resolveFrom(
        context,
      ),
      child: CustomScrollView(
        slivers: [
          CupertinoSliverNavigationBar(
            largeTitle: Obx(
              () => Text(model.danceInfo.value.danceName ?? "Dance"),
            ),
            trailing: Row(
              mainAxisSize: MainAxisSize.min,
              children: [
                Obx(
                  () => CupertinoButton(
                    padding: const EdgeInsets.all(12),
                    child: SvgPicture.asset(
                      model.isLoop.value
                          ? "assets/repeat.fill.svg" //loopenableicon
                          : "assets/repeat.svg", //loopcloseicon
                      colorFilter: ColorFilter.mode(
                        model.isLoop.value
                            ? CupertinoTheme.of(context).primaryColor
                            : CupertinoColors.systemGrey,
                        BlendMode.srcIn,
                      ),
                    ),
                    onPressed: () {
                      //switchloopstate
                      model.isLoop.value = !model.isLoop.value;
                      //ifCurrentlyinplay,updatemusicloopstate
                      if (model.isRun.value &&
                          model.danceInfo.value.musicUrl?.isNotEmpty == true) {
                        MusicUtil.shared.setMusicLoop(model.isLoop.value);
                      }
                    },
                  ),
                ),
                //wholeplay/stopbutton
                CupertinoButton(
                  padding: const EdgeInsets.all(12),
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
          //DownPullrefresh
          CupertinoSliverRefreshControl(onRefresh: getDanceList),
          //dancelist
          SliverToBoxAdapter(
            child: Padding(
              padding: const EdgeInsets.all(15),
              child: ClipRRect(
                borderRadius: BorderRadius.circular(25),
                child: Container(
                  color: CupertinoColors.tertiarySystemBackground.resolveFrom(
                    context,
                  ),
                  child: Obx(
                    () => model.danceInfo.value.danceData.isNotEmpty
                        ? ListView.separated(
                            padding: EdgeInsets.zero,
                            shrinkWrap: true,
                            physics: const NeverScrollableScrollPhysics(),
                            itemBuilder: (context, index) =>
                                danceItemView(context, index),
                            itemCount: model.danceInfo.value.danceData.length,
                            separatorBuilder: (context, index) => Padding(
                              padding: const EdgeInsets.only(left: 15),
                              child: Container(
                                color: CupertinoColors.separator.resolveFrom(
                                  context,
                                ),
                                width: double.infinity,
                                height: 0.5,
                              ),
                            ),
                          )
                        : CupertinoListTile(
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
                            title: Center(
                              child: Icon(CupertinoIcons.add_circled),
                            ),
                          ),
                  ),
                ),
              ),
            ),
          ),
        ],
      ),
    );
  }

  ///danceFramelistItem(NoModify,keeporiginallogic)
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
                    Spacer(),
                    CupertinoButton(
                      padding: EdgeInsets.zero,
                      minimumSize: .zero,
                      child: const Icon(
                        CupertinoIcons.minus_circle,
                        color: CupertinoColors.separator,
                      ),
                      onPressed: () {
                        model.danceInfo.value.danceData.removeAt(index);
                        model.danceInfo.refresh();
                        saveDance();
                      },
                    ),
                    Spacer(),
                    CupertinoButton(
                      padding: EdgeInsets.zero,
                      minimumSize: .zero,
                      onPressed: () {
                        //Duplicate / Copycurrent frame
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
                    Spacer(),
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
            padding: const EdgeInsets.all(10),
            child: Row(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                const SizedBox(width: 80),
                Expanded(
                  child: Column(
                    mainAxisSize: MainAxisSize.min,
                    children: [
                      //orientationAngleshow
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
                      //orientationControl joystick
                      LayoutBuilder(
                        builder: (context, constraints) {
                          return Container(
                            height: constraints.maxWidth / 2,
                            decoration: BoxDecoration(
                              color: CupertinoColors.systemGroupedBackground
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
                      SizedBox(height: 10),

                      //LeftSidelight stripcolorselect
                      Row(
                        children: [
                          Text("Light strip left color", style: titleStyle),
                          const Spacer(),
                          CupertinoButton(
                            borderRadius: BorderRadius.circular(50),
                            color: CupertinoColors.systemGroupedBackground
                                .resolveFrom(context),
                            minimumSize: Size.zero,
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

                      //rightlight stripcolorselect
                      SizedBox(height: 10),

                      Row(
                        children: [
                          Text("Light strip right color", style: titleStyle),
                          const Spacer(),
                          CupertinoButton(
                            borderRadius: BorderRadius.circular(50),
                            color: CupertinoColors.systemGroupedBackground
                                .resolveFrom(context),
                            minimumSize: Size.zero,
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

                      SizedBox(height: 10),
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
                      SizedBox(height: 10),
                      //durationAdjustment sliderBlock
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

  //colorselectControllerpopup(NoModify,keeporiginallogic)
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
            saveDance(); //colorselectcompleteaftersavedata
          }
          return value;
        });
  }
}
