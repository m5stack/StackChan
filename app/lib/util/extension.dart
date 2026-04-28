/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

import 'dart:convert';
import 'dart:ui' as ui;

import 'package:flex_color_picker/flex_color_picker.dart';
import 'package:flutter/cupertino.dart';
import 'package:flutter/foundation.dart';
import 'package:image/image.dart' as img;

extension HexExtension on Uint8List {
  String toHexString() {
    return map(
      (byte) => byte.toRadixString(16).padLeft(2, '0'),
    ).join().toUpperCase();
  }
}

// NeedReplaceKeyValueFor
final projectStringReplacement = {
  "小智": "Xiaozhi",
  "Qwen3 实时": "Qwen3 235B (Fast)",
  "DeepSeek V3.1": "DeepSeek V3.1 (Powerful)",
  "DouBao Seed 1.6": "Doubao Seed 1.6 (Delayed)",
  "GLM 4.7（内测）": "GLM 4.7（Internal Test）",
  "Kimi-K2（内测）": "Kimi-K2（Internal Test）",
  "Doubao 2.0（内测）": "Doubao 2.0（Internal Test）",
  "Qwen3.5 397B（内测）": "Qwen3.5 397B（Internal Test）",
};

extension StringTool on String? {
  /// RegexReplaceString
  /// projectStringReplacement ThenReplaceAllMatchContent
  String? regularExpressionSubstitution() {
    // 1. NullValueDirectlyReturns null
    if (this == null) {
      return null;
    }

    // 2. Non-NullString
    String result = this!;

    // 3. IterateReplaceDictionary,ReplaceAllMatchItem
    for (final entry in projectStringReplacement.entries) {
      // Escape,AvoidRegex(,)
      final pattern = RegExp.escape(entry.key);
      // GlobalReplaceAllMatchContent
      result = result.replaceAll(RegExp(pattern), entry.value);
    }

    return result;
  }
}

extension StringToUint8List on String? {
  ///Convert String? to Uint8List
  Uint8List toUint8List() {
    if (this == null || this!.isEmpty) {
      return Uint8List(0);
    }
    return Uint8List.fromList(utf8.encode(this!));
  }

  ///Convert Hex string to Color object
  ///Supported formats: "0xFFFFFFFF", "#FFFFFF", "FFFFFF"
  Color hex() {
    if (this == null || this!.isEmpty) return CupertinoColors.transparent;

    String hexString = this!.toUpperCase().replaceAll("#", "");
    if (hexString.startsWith("0X")) {
      hexString = hexString.substring(2);
    }

    if (hexString.length == 6) {
      hexString = "FF$hexString";
    }

    final intValue = int.tryParse(hexString, radix: 16);
    return Color(intValue ?? 0x00000000);
  }
}

extension ColorExtension on Color? {
  ///Convert Color to hex string (e.g., #RRGGBB)
  String hexString() {
    if (this == null) return "#000000";

    //Extract RGB channels and convert to hex, ignore Alpha to match standard color codes
    String r = this!.red8bit.toRadixString(16).padLeft(2, '0');
    String g = this!.green8bit.toRadixString(16).padLeft(2, '0');
    String b = this!.blue8bit.toRadixString(16).padLeft(2, '0');

    return "#$r$g$b".toUpperCase();
  }
}

extension ImageExtension on Uint8List {
  Future<Uint8List?> compress({
    ui.Size? resolutionSize,
    double? memorySize,
    bool cropCenter = false,
  }) async {
    //Use compute isolation to avoid blocking UI thread when processing large images
    return compute(
      _compressImage,
      _CompressParams(
        bytes: this,
        resolutionSize: resolutionSize,
        memorySize: memorySize,
        cropCenter: cropCenter,
      ),
    );
  }

  Future<Uint8List?> compressToMemorySize(double memorySize) async {
    return compress(
      resolutionSize: null,
      memorySize: memorySize,
      cropCenter: false,
    );
  }
}

//Compression parameter wrapper (for compute isolation)
class _CompressParams {
  final Uint8List bytes;
  final ui.Size? resolutionSize;
  final double? memorySize;
  final bool cropCenter;

  _CompressParams({
    required this.bytes,
    this.resolutionSize,
    this.memorySize,
    required this.cropCenter,
  });
}

//Core compression logic (top-level function for compute isolation)
Future<Uint8List?> _compressImage(_CompressParams params) async {
  try {
    //1. Decode original image
    img.Image? originalImage = img.decodeImage(params.bytes);
    if (originalImage == null) return null; //Return null on decode failure

    img.Image processedImage = originalImage;

    //2. Handle resolution scaling/cropping (align with iOS logic)
    if (params.resolutionSize != null) {
      final targetWidth = params.resolutionSize!.width.toInt();
      final targetHeight = params.resolutionSize!.height.toInt();

      if (params.cropCenter) {
        //CropCenter=true: Scale to cover target size then center crop (Aspect-Fill)
        final scaleX = targetWidth / originalImage.width;
        final scaleY = targetHeight / originalImage.height;
        final scale = scaleX > scaleY
            ? scaleX
            : scaleY; //Take larger scale ratio

        //Scale image to cover target size
        final scaledWidth = (originalImage.width * scale).toInt();
        final scaledHeight = (originalImage.height * scale).toInt();
        final scaledImage = img.copyResize(
          originalImage,
          width: scaledWidth,
          height: scaledHeight,
        );

        //Calculate center crop offset
        final cropX = (scaledWidth - targetWidth) ~/ 2;
        final cropY = (scaledHeight - targetHeight) ~/ 2;

        //Execute center crop
        processedImage = img.copyCrop(
          scaledImage,
          x: cropX,
          y: cropY,
          width: targetWidth,
          height: targetHeight,
        );
      } else {
        //CropCenter=false: Aspect-Fit scaling, draw to target size canvas
        final scaleX = targetWidth / originalImage.width;
        final scaleY = targetHeight / originalImage.height;
        final scale = scaleX < scaleY
            ? scaleX
            : scaleY; //Take smaller scale ratio

        //Scale image to fit target size
        final newWidth = (originalImage.width * scale).toInt();
        final newHeight = (originalImage.height * scale).toInt();
        final scaledImage = img.copyResize(
          originalImage,
          width: newWidth,
          height: newHeight,
        );

        //Create target size canvas, draw scaled image at top-left (align with iOS draw logic)
        final canvas = img.Image(width: targetWidth, height: targetHeight);
        //Critical fix: Use direct blend mode (normal overlay, no color blending)
        img.compositeImage(
          canvas,
          scaledImage,
          dstX: 0,
          dstY: 0,
          blend: img.BlendMode.direct,
        );
        processedImage = canvas;
      }
    }

    //3. Handle memory size compression (JPEG quality adjustment)
    if (params.memorySize == null) {
      //No memory limit, return 100% quality JPEG
      return img.encodeJpg(processedImage, quality: 100);
    }

    //Calculate max bytes (MB → Bytes)
    final maxBytes = (params.memorySize! * 1024 * 1024).toInt();
    int quality = 100; //Corresponds to iOS compressionQuality=1.0
    //Null safety fix: encodeJpg may return null, need ? and handling
    Uint8List compressedData = img.encodeJpg(processedImage, quality: quality);
    //Gradually reduce quality (×0.7 each time) until size limit met or quality below 1%
    while (compressedData.length > maxBytes && quality > 1) {
      quality = (quality * 0.7).round();
      if (quality < 1) quality = 1; //Minimum quality limit is 1%
      compressedData = img.encodeJpg(processedImage, quality: quality);
    }

    return compressedData;
  } catch (e) {
        return null;
  }
}
