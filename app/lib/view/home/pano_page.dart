/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

import 'dart:typed_data';

import 'package:flutter/cupertino.dart';
import 'package:get/get.dart';
import 'package:opencv_dart/opencv.dart' as cv;
import 'package:stack_chan/app_state.dart';

import '../../model/expression_data.dart';
import '../../network/web_socket_util.dart';
import '../../util/extension.dart';

class PanoPage extends StatefulWidget {
  const PanoPage({super.key});

  @override
  State<StatefulWidget> createState() => _PanoPageState();
}

class _PanoPageState extends State<PanoPage> {
  final String tag = "PanoPage";

  bool recordSwitch = false;

  RxList<Uint8List> imageDataList = RxList([]);

  Rxn<Uint8List> panoImage = Rxn();

  RxBool isTakingPhotos = false.obs;
  RxBool isLoading = false.obs;
  final Duration motionDelay = Duration(milliseconds: 500);
  final Duration captureDelay = Duration(milliseconds: 500);

  final SliverGridDelegateWithFixedCrossAxisCount gridDelegate =
      SliverGridDelegateWithFixedCrossAxisCount(
        crossAxisCount: 3,
        crossAxisSpacing: 8,
        mainAxisSpacing: 8,
        childAspectRatio: 1,
      );

  //舵机运动轨迹data
  List<MotionData> motionList = [
    //1
    MotionData(
      pitchServo: MotionDataItem(angle: 0, speed: 0),
      yawServo: MotionDataItem(angle: 900, speed: 0),
    ),

    //2 * 7
    MotionData(
      pitchServo: MotionDataItem(angle: 1280, speed: 0),
      yawServo: MotionDataItem(angle: 675, speed: 0),
    ),
    MotionData(
      pitchServo: MotionDataItem(angle: 853, speed: 0),
      yawServo: MotionDataItem(angle: 675, speed: 0),
    ),
    MotionData(
      pitchServo: MotionDataItem(angle: 426, speed: 0),
      yawServo: MotionDataItem(angle: 675, speed: 0),
    ),
    MotionData(
      pitchServo: MotionDataItem(angle: 0, speed: 0),
      yawServo: MotionDataItem(angle: 675, speed: 0),
    ),
    MotionData(
      pitchServo: MotionDataItem(angle: -426, speed: 0),
      yawServo: MotionDataItem(angle: 675, speed: 0),
    ),
    MotionData(
      pitchServo: MotionDataItem(angle: -853, speed: 0),
      yawServo: MotionDataItem(angle: 675, speed: 0),
    ),
    MotionData(
      pitchServo: MotionDataItem(angle: -1280, speed: 0),
      yawServo: MotionDataItem(angle: 675, speed: 0),
    ),

    //3 * 7
    MotionData(
      pitchServo: MotionDataItem(angle: -1280, speed: 0),
      yawServo: MotionDataItem(angle: 450, speed: 0),
    ),
    MotionData(
      pitchServo: MotionDataItem(angle: -853, speed: 0),
      yawServo: MotionDataItem(angle: 450, speed: 0),
    ),
    MotionData(
      pitchServo: MotionDataItem(angle: -426, speed: 0),
      yawServo: MotionDataItem(angle: 450, speed: 0),
    ),
    MotionData(
      pitchServo: MotionDataItem(angle: 0, speed: 0),
      yawServo: MotionDataItem(angle: 450, speed: 0),
    ),
    MotionData(
      pitchServo: MotionDataItem(angle: 426, speed: 0),
      yawServo: MotionDataItem(angle: 450, speed: 0),
    ),
    MotionData(
      pitchServo: MotionDataItem(angle: 853, speed: 0),
      yawServo: MotionDataItem(angle: 450, speed: 0),
    ),
    MotionData(
      pitchServo: MotionDataItem(angle: 1280, speed: 0),
      yawServo: MotionDataItem(angle: 450, speed: 0),
    ),

    //4 * 7
    MotionData(
      pitchServo: MotionDataItem(angle: 1280, speed: 0),
      yawServo: MotionDataItem(angle: 225, speed: 0),
    ),
    MotionData(
      pitchServo: MotionDataItem(angle: 853, speed: 0),
      yawServo: MotionDataItem(angle: 225, speed: 0),
    ),
    MotionData(
      pitchServo: MotionDataItem(angle: 426, speed: 0),
      yawServo: MotionDataItem(angle: 225, speed: 0),
    ),
    MotionData(
      pitchServo: MotionDataItem(angle: 0, speed: 0),
      yawServo: MotionDataItem(angle: 225, speed: 0),
    ),
    MotionData(
      pitchServo: MotionDataItem(angle: -426, speed: 0),
      yawServo: MotionDataItem(angle: 225, speed: 0),
    ),
    MotionData(
      pitchServo: MotionDataItem(angle: -853, speed: 0),
      yawServo: MotionDataItem(angle: 225, speed: 0),
    ),
    MotionData(
      pitchServo: MotionDataItem(angle: -1280, speed: 0),
      yawServo: MotionDataItem(angle: 225, speed: 0),
    ),

    //5 * 7
    MotionData(
      pitchServo: MotionDataItem(angle: -1280, speed: 0),
      yawServo: MotionDataItem(angle: 0, speed: 0),
    ),
    MotionData(
      pitchServo: MotionDataItem(angle: -853, speed: 0),
      yawServo: MotionDataItem(angle: 0, speed: 0),
    ),
    MotionData(
      pitchServo: MotionDataItem(angle: -426, speed: 0),
      yawServo: MotionDataItem(angle: 0, speed: 0),
    ),
    MotionData(
      pitchServo: MotionDataItem(angle: 0, speed: 0),
      yawServo: MotionDataItem(angle: 0, speed: 0),
    ),
    MotionData(
      pitchServo: MotionDataItem(angle: 426, speed: 0),
      yawServo: MotionDataItem(angle: 0, speed: 0),
    ),
    MotionData(
      pitchServo: MotionDataItem(angle: 853, speed: 0),
      yawServo: MotionDataItem(angle: 0, speed: 0),
    ),
    MotionData(
      pitchServo: MotionDataItem(angle: 1280, speed: 0),
      yawServo: MotionDataItem(angle: 0, speed: 0),
    ),
  ];

  @override
  void initState() {
    super.initState();
    WebSocketUtil.shared.addObserver(tag, (message) {
      if (message is Uint8List) {
        final result = AppState.shared.parseMessage(message);
        final msgType = result.$1;
        final parsedData = result.$2;
        if (msgType != null) {
          switch (msgType) {
            case .jpeg:
              if (parsedData != null) {
                if (recordSwitch) {
                  imageDataList.add(parsedData);
                  recordSwitch = false;
                }
              }
              break;
            default:
              break;
          }
        }
      }
    });
  }

  @override
  void dispose() {
    WebSocketUtil.shared.removeObserver(tag);
    super.dispose();
  }

  Future<void> startTakingPhotos() async {
    if (AppState.shared.deviceMac.isEmpty) {
      AppState.shared.showToast("Please re-attempt after binding the device.");
      return;
    }

    if (isTakingPhotos.value) return;

    try {
      isTakingPhotos.value = true;
      imageDataList.clear();
      panoImage.value = null;
      AppState.shared.showToast("Start panoramic shooting...");

      AppState.shared.sendWebSocketMessage(
        .onCamera,
        data: AppState.shared.deviceMac.toUint8List(),
      );
      await Future.delayed(Duration(milliseconds: 300));

      for (final motion in motionList) {
        final jsonString = AppState.shared.deviceMac + motion.toString();
        AppState.shared.sendWebSocketMessage(
          .controlMotion,
          data: jsonString.toUint8List(),
        );

        await Future.delayed(motionDelay);

        recordSwitch = true;

        await Future.delayed(captureDelay);
      }

      AppState.shared.sendWebSocketMessage(
        .offCamera,
        data: AppState.shared.deviceMac.toUint8List(),
      );

      AppState.shared.showToast("The shooting is complete.");

      startAssemble();
    } catch (e) {
      AppState.shared.sendWebSocketMessage(
        .offCamera,
        data: AppState.shared.deviceMac.toUint8List(),
      );
      AppState.shared.showToast("The shooting was unsuccessful.：${e.toString()}");
      debugPrint("拍照Exception:$e");
    } finally {
      isTakingPhotos.value = false;
      recordSwitch = false;
    }

    ///打开deviceSide / EndCamera
    AppState.shared.sendWebSocketMessage(
      .onCamera,
      data: AppState.shared.deviceMac.toUint8List(),
    );

    for (final motion in motionList) {
      String jsonString = AppState.shared.deviceMac + motion.toString();
      AppState.shared.sendWebSocketMessage(
        .controlMotion,
        data: jsonString.toUint8List(),
      );

      ///Wait500ms,然after  recordSwitch = true, 接着Again等 500ms, 接着executeNext
    }

    ///closedeviceSide / EndCamera
    AppState.shared.sendWebSocketMessage(
      .offCamera,
      data: AppState.shared.deviceMac.toUint8List(),
    );
  }

  Future<void> startAssemble() async {
    if (imageDataList.length < 5) {
      AppState.shared.showToast("At least 5 photos are needed to stitch together a panoramic image!");
      return;
    }
    if (isLoading.value) return;
    isLoading.value = true;
    panoImage.value = null;

    List<cv.Mat> mats = [];
    cv.VecMat? vecMat;
    cv.Stitcher? stitcher;

    try {
      for (final data in imageDataList) {
        final mat = await cv.imdecodeAsync(data, cv.IMREAD_COLOR);
        if (mat.isEmpty) {
          throw Exception("Invalid image data");
        }
        mats.add(mat);
      }
      vecMat = cv.VecMat.fromList(mats);
      stitcher = cv.Stitcher.create(mode: .PANORAMA);
      final (status, result) = await stitcher.stitchAsync(vecMat);
      if (status != cv.StitcherStatus.OK) {
        throw Exception("Stitch error code: $status");
      }
      final (resultStatus, jpeg) = await cv.imencodeAsync(".jpg", result);
      if (!resultStatus) {
        throw Exception("Encode failed");
      }
      panoImage.value = jpeg;
      AppState.shared.showToast("Stitch success!");
      result.dispose();
    } catch (e) {
      AppState.shared.showToast("Error: ${e.toString()}");
      debugPrint("Stitch error: $e");
    } finally {
      for (var mat in mats) {
        mat.dispose();
      }
      vecMat?.dispose();
      stitcher?.dispose();
      isLoading.value = false;
    }
  }

  Widget buildImageItem(BuildContext context, int index) {
    final data = imageDataList[index];
    return Stack(
      fit: .expand,
      children: [
        ClipRRect(
          borderRadius: .circular(8),
          child: Image.memory(data, fit: .cover),
        ),
        Positioned(
          top: 4,
          right: 4,
          child: CupertinoButton(
            padding: .zero,
            minimumSize: Size(26, 26),
            child: Icon(
              CupertinoIcons.clear_circled_solid,
              color: CupertinoColors.systemRed,
              size: 26,
            ),
            onPressed: () {
              imageDataList.removeAt(index);
            },
          ),
        ),
      ],
    );
  }

  @override
  Widget build(BuildContext context) {
    return CupertinoPageScaffold(
      backgroundColor: CupertinoColors.systemBackground.resolveFrom(context),
      navigationBar: CupertinoNavigationBar.large(
        largeTitle: Text("Panorama"),
        backgroundColor: CupertinoColors.systemBackground.resolveFrom(context),
      ),
      child: Padding(
        padding: .only(
          top: 15,
          bottom: 15 + MediaQuery.paddingOf(context).bottom,
          left: 15,
          right: 15,
        ),
        child: Column(
          children: [
            Obx(() {
              if (panoImage.value != null) {
                return Column(
                  mainAxisSize: .min,
                  spacing: 8,
                  children: [
                    Text("Panorama Result"),
                    Container(
                      height: 220,
                      decoration: BoxDecoration(
                        borderRadius: .circular(10),
                        image: DecorationImage(
                          image: MemoryImage(panoImage.value!),
                          fit: .contain,
                        ),
                      ),
                    ),
                  ],
                );
              }
              return SizedBox.shrink();
            }),
            Expanded(
              child: Obx(() {
                if (imageDataList.isEmpty) {
                  return const Center(child: Text("No photos, take first"));
                }
                return GridView.builder(
                  itemCount: imageDataList.length,
                  gridDelegate: gridDelegate,
                  itemBuilder: buildImageItem,
                );
              }),
            ),
            CupertinoButton.filled(
              child: const SizedBox(
                width: double.infinity,
                child: Center(child: Text("Generate")),
              ),
              onPressed: () {
                startTakingPhotos();
              },
            ),
          ],
        ),
      ),
    );
  }
}
