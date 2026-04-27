/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

import 'package:camera/camera.dart';
import 'package:flutter/cupertino.dart';
import 'package:google_mlkit_face_detection/google_mlkit_face_detection.dart';

class MlKitUtil {
  MlKitUtil._internal();

  static final MlKitUtil shared = MlKitUtil._internal();

  late final FaceDetector _faceDetector = FaceDetector(
    options: FaceDetectorOptions(
      enableClassification: true,
      enableTracking: true,
      performanceMode: .accurate,
    ),
  );

  bool _isProcessing = false;

  Future<void> testing(
    CameraImage image,
    int sensorOrientation,
    Function(List<Face>) onFacesDetected,
  ) async {
    if (_isProcessing) return;
    _isProcessing = true;
    try {
      final inputImage = _convertToInputImage(image, sensorOrientation);
      final faces = await _faceDetector.processImage(inputImage);
      onFacesDetected(faces);
    } catch (e) {
      debugPrint("Face detection error: $e");
    } finally {
      _isProcessing = false;
    }
  }

  InputImage _convertToInputImage(CameraImage image, int sensorOrientation) {
    return InputImage.fromBytes(
      bytes: image.planes.first.bytes,
      metadata: InputImageMetadata(
        size: Size(image.width.toDouble(), image.height.toDouble()),
        rotation: _getRotation(sensorOrientation),
        format: .nv21,
        bytesPerRow: image.planes[0].bytesPerRow,
      ),
    );
  }

  InputImageRotation _getRotation(int degrees) {
    switch (degrees) {
      case 90:
        return InputImageRotation.rotation90deg;
      case 180:
        return InputImageRotation.rotation180deg;
      case 270:
        return InputImageRotation.rotation270deg;
      default:
        return InputImageRotation.rotation0deg;
    }
  }
}
