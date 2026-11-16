/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

import 'dart:typed_data';
import 'dart:ui' as ui;

import 'package:cupertino_ui/cupertino_ui.dart';
import 'package:stack_chan/view/util/theme_setting.dart';
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
  bool _shouldAutoAssemble = false;
  final Duration motionDelay = const Duration(milliseconds: 500);
  final Duration captureDelay = const Duration(milliseconds: 500);

  final SliverGridDelegateWithFixedCrossAxisCount gridDelegate =
      const SliverGridDelegateWithFixedCrossAxisCount(
        crossAxisCount: 3,
        crossAxisSpacing: 8,
        mainAxisSpacing: 8,
        childAspectRatio: 1,
      );

  // Servo poses visited during the panorama sweep.
  List<MotionData> motionList = [
    //1
    MotionData(
      yawServo: MotionDataItem(angle: 0, speed: 0),
      pitchServo: MotionDataItem(angle: 900, speed: 0),
    ),

    //2 * 7
    MotionData(
      yawServo: MotionDataItem(angle: 1280, speed: 0),
      pitchServo: MotionDataItem(angle: 675, speed: 0),
    ),
    MotionData(
      yawServo: MotionDataItem(angle: 853, speed: 0),
      pitchServo: MotionDataItem(angle: 675, speed: 0),
    ),
    MotionData(
      yawServo: MotionDataItem(angle: 426, speed: 0),
      pitchServo: MotionDataItem(angle: 675, speed: 0),
    ),
    MotionData(
      yawServo: MotionDataItem(angle: 0, speed: 0),
      pitchServo: MotionDataItem(angle: 675, speed: 0),
    ),
    MotionData(
      yawServo: MotionDataItem(angle: -426, speed: 0),
      pitchServo: MotionDataItem(angle: 675, speed: 0),
    ),
    MotionData(
      yawServo: MotionDataItem(angle: -853, speed: 0),
      pitchServo: MotionDataItem(angle: 675, speed: 0),
    ),
    MotionData(
      yawServo: MotionDataItem(angle: -1280, speed: 0),
      pitchServo: MotionDataItem(angle: 675, speed: 0),
    ),

    // 3 * 7
    MotionData(
      yawServo: MotionDataItem(angle: -1280, speed: 0),
      pitchServo: MotionDataItem(angle: 450, speed: 0),
    ),
    MotionData(
      yawServo: MotionDataItem(angle: -853, speed: 0),
      pitchServo: MotionDataItem(angle: 450, speed: 0),
    ),
    MotionData(
      yawServo: MotionDataItem(angle: -426, speed: 0),
      pitchServo: MotionDataItem(angle: 450, speed: 0),
    ),
    MotionData(
      yawServo: MotionDataItem(angle: 0, speed: 0),
      pitchServo: MotionDataItem(angle: 450, speed: 0),
    ),
    MotionData(
      yawServo: MotionDataItem(angle: 426, speed: 0),
      pitchServo: MotionDataItem(angle: 450, speed: 0),
    ),
    MotionData(
      yawServo: MotionDataItem(angle: 853, speed: 0),
      pitchServo: MotionDataItem(angle: 450, speed: 0),
    ),
    MotionData(
      yawServo: MotionDataItem(angle: 1280, speed: 0),
      pitchServo: MotionDataItem(angle: 450, speed: 0),
    ),

    // //4 * 7
    // MotionData(
    //   yawServo: MotionDataItem(angle: 1280, speed: 0),
    //   pitchServo: MotionDataItem(angle: 225, speed: 0),
    // ),
    // MotionData(
    //   yawServo: MotionDataItem(angle: 853, speed: 0),
    //   pitchServo: MotionDataItem(angle: 225, speed: 0),
    // ),
    // MotionData(
    //   yawServo: MotionDataItem(angle: 426, speed: 0),
    //   pitchServo: MotionDataItem(angle: 225, speed: 0),
    // ),
    // MotionData(
    //   yawServo: MotionDataItem(angle: 0, speed: 0),
    //   pitchServo: MotionDataItem(angle: 225, speed: 0),
    // ),
    // MotionData(
    //   yawServo: MotionDataItem(angle: -426, speed: 0),
    //   pitchServo: MotionDataItem(angle: 225, speed: 0),
    // ),
    // MotionData(
    //   yawServo: MotionDataItem(angle: -853, speed: 0),
    //   pitchServo: MotionDataItem(angle: 225, speed: 0),
    // ),
    // MotionData(
    //   yawServo: MotionDataItem(angle: -1280, speed: 0),
    //   pitchServo: MotionDataItem(angle: 225, speed: 0),
    // ),
    //
    // //5 * 7
    // MotionData(
    //   yawServo: MotionDataItem(angle: -1280, speed: 0),
    //   pitchServo: MotionDataItem(angle: 0, speed: 0),
    // ),
    // MotionData(
    //   yawServo: MotionDataItem(angle: -853, speed: 0),
    //   pitchServo: MotionDataItem(angle: 0, speed: 0),
    // ),
    // MotionData(
    //   yawServo: MotionDataItem(angle: -426, speed: 0),
    //   pitchServo: MotionDataItem(angle: 0, speed: 0),
    // ),
    // MotionData(
    //   yawServo: MotionDataItem(angle: 0, speed: 0),
    //   pitchServo: MotionDataItem(angle: 0, speed: 0),
    // ),
    // MotionData(
    //   yawServo: MotionDataItem(angle: 426, speed: 0),
    //   pitchServo: MotionDataItem(angle: 0, speed: 0),
    // ),
    // MotionData(
    //   yawServo: MotionDataItem(angle: 853, speed: 0),
    //   pitchServo: MotionDataItem(angle: 0, speed: 0),
    // ),
    // MotionData(
    //   yawServo: MotionDataItem(angle: 1280, speed: 0),
    //   pitchServo: MotionDataItem(angle: 0, speed: 0),
    // ),
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
                  if (_shouldAutoAssemble &&
                      imageDataList.length >= motionList.length &&
                      !isLoading.value) {
                    _shouldAutoAssemble = false;
                    startAssemble();
                  }
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
      _shouldAutoAssemble = false;
      imageDataList.clear();
      panoImage.value = null;
      AppState.shared.showToast("Start panoramic shooting...");

      AppState.shared.sendWebSocketMessage(
        .onCamera,
        data: AppState.shared.deviceMac.toUint8List(),
      );
      await Future.delayed(const Duration(milliseconds: 300));

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

      _shouldAutoAssemble = true;
      if (imageDataList.length >= motionList.length && !isLoading.value) {
        _shouldAutoAssemble = false;
        startAssemble();
      }
    } catch (e) {
      AppState.shared.sendWebSocketMessage(
        .offCamera,
        data: AppState.shared.deviceMac.toUint8List(),
      );
      _shouldAutoAssemble = false;
      AppState.shared.showToast(
        "The shooting was unsuccessful.：${e.toString()}",
      );
      debugPrint("拍照Exception:$e");
    } finally {
      isTakingPhotos.value = false;
      recordSwitch = false;
    }
  }

  Future<void> startAssemble() async {
    if (imageDataList.length < 5) {
      AppState.shared.showToast(
        "At least 5 photos are needed to stitch together a panoramic image!",
      );
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
      debugPrint("Stitch error: $e");
      try {
        final fallbackImage = await _mergeImagesHorizontally(imageDataList);
        panoImage.value = fallbackImage;
        AppState.shared.showToast(
          "Native Stitcher unavailable, used simple merge result.",
        );
      } catch (fallbackError) {
        AppState.shared.showToast("Error: ${fallbackError.toString()}");
        debugPrint("Fallback stitch error: $fallbackError");
      }
    } finally {
      for (var mat in mats) {
        mat.dispose();
      }
      vecMat?.dispose();
      stitcher?.dispose();
      isLoading.value = false;
    }
  }

  Future<Uint8List> _mergeImagesHorizontally(List<Uint8List> images) async {
    if (images.isEmpty) {
      throw Exception("No images to merge");
    }

    final decodedImages = <ui.Image>[];
    try {
      for (final bytes in images) {
        final codec = await ui.instantiateImageCodec(bytes);
        final frame = await codec.getNextFrame();
        decodedImages.add(frame.image);
      }

      final totalWidth = decodedImages.fold<int>(
        0,
        (sum, image) => sum + image.width,
      );
      final maxHeight = decodedImages.fold<int>(
        0,
        (max, image) => image.height > max ? image.height : max,
      );

      final recorder = ui.PictureRecorder();
      final canvas = ui.Canvas(recorder);
      final paint = Paint();

      double dx = 0;
      for (final image in decodedImages) {
        final dy = (maxHeight - image.height) / 2;
        canvas.drawImage(image, Offset(dx, dy), paint);
        dx += image.width;
      }

      final picture = recorder.endRecording();
      final merged = await picture.toImage(totalWidth, maxHeight);
      final byteData = await merged.toByteData(format: ui.ImageByteFormat.png);
      merged.dispose();
      if (byteData == null) {
        throw Exception("Encode merged image failed");
      }
      return byteData.buffer.asUint8List();
    } finally {
      for (final image in decodedImages) {
        image.dispose();
      }
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
            child: const Icon(
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
    final padding = MediaQuery.paddingOf(context);

    return CupertinoPageScaffold(
      backgroundColor: CupertinoColors.systemGroupedBackground.resolveFrom(
        context,
      ),
      navigationBar: CupertinoNavigationBar.large(
        backgroundColor: ThemeSetting.cupertinoNavigationBarBackgroundColor(
          context,
        ),
        border: ThemeSetting.cupertinoNavigationBarBorder(context),
        padding: ThemeSetting.navigationBarPadding,
        largeTitle: const Text("Panorama"),
      ),
      child: Padding(
        padding: .only(
          top: 20,
          bottom: 20 + padding.bottom,
          left: 20 + padding.left,
          right: 20 + padding.right,
        ),
        child: Column(
          children: [
            Obx(() {
              if (panoImage.value != null) {
                return Column(
                  mainAxisSize: .min,
                  spacing: 8,
                  children: [
                    const Text("Panorama Result"),
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
              return const SizedBox.shrink();
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
              child: const Center(
                child: Text(
                  "Generate",
                  style: TextStyle(color: CupertinoColors.white),
                ),
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
