/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

import 'dart:typed_data';

import 'package:camera/camera.dart';
import 'package:flutter/cupertino.dart';
import 'package:google_mlkit_face_detection/google_mlkit_face_detection.dart';
import 'package:opencv_dart/opencv_dart.dart' as cv;
import 'package:stack_chan/model/expression_data.dart';
import 'package:stack_chan/util/ml_kit_util.dart';

import '../../model/dance_list.dart';

class StackChanFaceView extends StatefulWidget {
  const StackChanFaceView({
    super.key,
    required this.captureScreen,
    this.onFrameCallback,
    this.onCallback,
  });

  final bool captureScreen; //output画面
  final Function(Uint8List)? onFrameCallback; //画面outputcallback
  final Function(DanceData)? onCallback; //检测datacallback

  @override
  State<StatefulWidget> createState() => _StackChanFaceViewState();
}

class _StackChanFaceViewState extends State<StackChanFaceView> {
  CameraController? cameraController;

  DateTime lastProcessTime = DateTime.now();
  bool isProcessing = false;

  @override
  void initState() {
    super.initState();
    initCamera();
  }

  Future<void> initCamera() async {
    List<CameraDescription> cameras = await availableCameras();
    if (cameras.isEmpty) return;

    CameraDescription? frontCamera;
    for (var i in cameras) {
      if (i.lensDirection == .front) {
        frontCamera = i;
        break;
      }
    }
    if (frontCamera == null) {
      return;
    }

    cameraController = CameraController(
      frontCamera,
      .medium,
      imageFormatGroup: .nv21,
    );

    cameraController!
        .initialize()
        .then((_) async {
          if (!mounted) {
            return;
          }
          await cameraController!.startImageStream((image) {
            processCameraImage(image, frontCamera!.sensorOrientation);
          });
          setState(() {});
        })
        .catchError((Object e) {
          if (e is CameraException) {
            switch (e.code) {
              case "CameraAccessDenied":
                // Handle access errors here.
                break;
              default:
                // Handle other errors here.
                break;
            }
          }
        });
  }

  ///检测faceand导出画Surface / Side
  void processCameraImage(CameraImage image, int sensorOrientation) {
    ///检测face
    MlKitUtil.shared.testing(image, sensorOrientation, (faces) {
      if (faces.isNotEmpty) {
        dataConversionTesting(faces.first);
      }
    });
    if (widget.captureScreen) {
      //willimagecompressConcurrencyBack去
      final now = DateTime.now();
      if (now.difference(lastProcessTime).inMilliseconds >= 100) {
        if (isProcessing) return;
        lastProcessTime = now;
        isProcessing = true;
        handleAsyncCompression(image, sensorOrientation);
      }
    }
  }

  ///willdataconvert
  void dataConversionTesting(Face face) {
    double headYaw = face.headEulerAngleY ?? 0;
    double headPitch = face.headEulerAngleX ?? 0;

    int yawServoAngle = (headYaw * -20).toInt().clamp(-1280, 1280);
    int pitchServoAngle = (headPitch * 10).toInt().clamp(0, 900);

    double leftEyeProb = face.leftEyeOpenProbability ?? 1.0;
    double rightEyeProb = face.rightEyeOpenProbability ?? 1.0;

    int leftWeight = (leftEyeProb * 100).toInt().clamp(0, 100);
    int rightWeight = (rightEyeProb * 100).toInt().clamp(0, 100);

    double smileProb = face.smilingProbability ?? 0.0;
    int mouthWeight = (smileProb * 100).toInt().clamp(0, 100);

    ExpressionItem leftEye = ExpressionItem(
      x: 0,
      y: 0,
      rotation: 0,
      weight: leftWeight,
    );
    ExpressionItem rightEye = ExpressionItem(
      x: 0,
      y: 0,
      rotation: 0,
      weight: rightWeight,
    );
    ExpressionItem mouth = ExpressionItem(
      x: 0,
      y: 0,
      rotation: 0,
      weight: mouthWeight,
    );

    if (smileProb > 0.3) {
      leftEye.weight = (leftEye.weight - 35).clamp(0, 100);
      leftEye.rotation = -2150;
      rightEye.weight = (rightEye.weight - 35).clamp(0, 100);
      rightEye.rotation = 2150;
    } else if (smileProb < 0.1 && (leftEyeProb < 0.5 || rightEyeProb < 0.5)) {
      leftEye.rotation = 450;
      rightEye.rotation = -450;
    }

    DanceData data = DanceData(
      leftEye: leftEye,
      rightEye: rightEye,
      mouth: mouth,
      yawServo: MotionDataItem(angle: yawServoAngle),
      pitchServo: MotionDataItem(angle: pitchServoAngle),
      durationMs: 1000,
    );

    if (widget.onCallback != null) {
      widget.onCallback!(data);
    }
  }

  ///compressAndsend出去
  Future<void> handleAsyncCompression(
    CameraImage image,
    int sensorOrientation,
  ) async {
    final nv21Bytes = image.planes.first.bytes;
    final mat = cv.Mat.fromList(
      (image.height * 1.5).toInt(),
      image.width,
      .CV_8UC1,
      nv21Bytes,
    );
    final bgrMat = cv.cvtColor(mat, cv.COLOR_YUV2BGR_NV21);
    final rotatedMat = rotateMatIfNeeded(bgrMat, sensorOrientation);
    final (success, jpegByte) = cv.imencode(".jpg", rotatedMat);
    if (success) {
      if (jpegByte.isNotEmpty && widget.onFrameCallback != null) {
        widget.onFrameCallback!(jpegByte);
      }
    }
    mat.dispose();
    bgrMat.dispose();
    rotatedMat.dispose();
    isProcessing = false;
  }

  cv.Mat rotateMatIfNeeded(cv.Mat src, int orientation) {
    if (orientation == 90) {
      return cv.rotate(src, cv.ROTATE_90_CLOCKWISE);
    } else if (orientation == 180) {
      return cv.rotate(src, cv.ROTATE_180);
    } else if (orientation == 270) {
      return cv.rotate(src, cv.ROTATE_90_COUNTERCLOCKWISE);
    }
    return src;
  }

  @override
  void dispose() {
    cameraController?.stopImageStream();
    cameraController?.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    if (cameraController == null) {
      return Center(child: CupertinoActivityIndicator());
    } else {
      return SizedBox.expand(child: CameraPreview(cameraController!));
    }
  }
}
