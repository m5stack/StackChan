/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

import 'dart:convert';
import 'dart:ui' as ui;

import 'package:flex_color_picker/flex_color_picker.dart';
import 'package:cupertino_ui/cupertino_ui.dart';
import 'package:flutter/foundation.dart';
import 'package:image/image.dart' as img;

extension HexExtension on Uint8List {
  String toHexString() {
    return map(
      (byte) => byte.toRadixString(16).padLeft(2, '0'),
    ).join().toUpperCase();
  }
}

// English display names for known model names.
final projectStringReplacement = {
  "小智": "Xiaozhi",
  "Qwen3 实时": "Qwen3 235B (Fast)",
  "DeepSeek V3.1": "DeepSeek V3.1 (Powerful)",
  "DouBao Seed 1.6": "Doubao Seed 1.6 (Delayed)",
  "GLM 4.7（内测）": "GLM 4.7（Internal Test）",
  "Kimi-K2（内测）": "Kimi-K2（Internal Test）",
  "Doubao 2.0（内测）": "Doubao 2.0（Internal Test）",
  "Qwen3.5 397B（内测）": "Qwen3.5 397B（Internal Test）",
  "DeepSeek V4": "DeepSeek V4 (Fast)",
  "DouBao Seed 2.0": "DouBao Seed 2.0 (Delayed)",
};

extension StringTool on String? {
  /// Replaces each known model name with its English display name.
  String? regularExpressionSubstitution() {
    // Return null unchanged.
    if (this == null) {
      return null;
    }

    // Work with a non-null copy.
    String result = this!;

    // Replace each mapped term.
    for (final entry in projectStringReplacement.entries) {
      // Escape regex metacharacters such as parentheses and dots.
      final pattern = RegExp.escape(entry.key);
      // Replace every occurrence of the key.
      result = result.replaceAll(RegExp(pattern), entry.value);
    }

    return result;
  }
}

extension StringToUint8List on String? {
  /// Encode this string as UTF-8 bytes.
  Uint8List toUint8List() {
    if (this == null || this!.isEmpty) {
      return Uint8List(0);
    }
    return Uint8List.fromList(utf8.encode(this!));
  }

  /// Parse a hex color string.
  /// Accepts "0xFFFFFFFF", "#FFFFFF", and "FFFFFF".
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
  /// Format the color as #RRGGBB.
  String hexString() {
    if (this == null) return "#000000";

    // Convert RGB channels to hex and omit alpha.
    String r = this!.red8bit.toRadixString(16).padLeft(2, '0');
    String g = this!.green8bit.toRadixString(16).padLeft(2, '0');
    String b = this!.blue8bit.toRadixString(16).padLeft(2, '0');

    return "#$r$g$b".toUpperCase();
  }

  Color desaturateColor(double saturation) {
    if (this == null) {
      return CupertinoColors.transparent;
    }
    HSVColor hsv = HSVColor.fromColor(this!);
    return hsv.withSaturation(saturation).toColor();
  }

  Color brightenColor(double brightness) {
    if (this == null) {
      return CupertinoColors.transparent;
    }

    // Blend toward white: 0.0 leaves the color unchanged and 1.0 makes it
    // pure white while preserving alpha.
    final factor = brightness.clamp(0.0, 1.0);
    final color = this!;

    final r = (color.red8bit + (255 - color.red8bit) * factor).round();
    final g = (color.green8bit + (255 - color.green8bit) * factor).round();
    final b = (color.blue8bit + (255 - color.blue8bit) * factor).round();

    return Color.fromARGB(color.alpha8bit, r, g, b);
  }
}

extension ImageExtension on Uint8List {
  Future<Uint8List?> compress({
    ui.Size? resolutionSize,
    double? memorySize,
    bool cropCenter = false,
  }) async {
    // Compress on a background isolate to keep the UI responsive.
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

// Parameters passed to the image compression isolate.
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

// Compress image bytes on a background isolate.
Future<Uint8List?> _compressImage(_CompressParams params) async {
  try {
    // Decode the source image.
    img.Image? originalImage = img.decodeImage(params.bytes);
    if (originalImage == null) return null; // Return null if decoding fails.

    img.Image processedImage = originalImage;

    // Scale and crop to match the iOS implementation.
    if (params.resolutionSize != null) {
      final targetWidth = params.resolutionSize!.width.toInt();
      final targetHeight = params.resolutionSize!.height.toInt();

      if (params.cropCenter) {
        // Center-crop after scaling to cover the target size.
        final scaleX = targetWidth / originalImage.width;
        final scaleY = targetHeight / originalImage.height;
        final scale = scaleX > scaleY
            ? scaleX
            : scaleY; // Use the larger scale factor.

        // Scale the image to cover the target size.
        final scaledWidth = (originalImage.width * scale).toInt();
        final scaledHeight = (originalImage.height * scale).toInt();
        final scaledImage = img.copyResize(
          originalImage,
          width: scaledWidth,
          height: scaledHeight,
        );

        // Calculate the top-left crop offset.
        final cropX = (scaledWidth - targetWidth) ~/ 2;
        final cropY = (scaledHeight - targetHeight) ~/ 2;

        // Crop the center region.
        processedImage = img.copyCrop(
          scaledImage,
          x: cropX,
          y: cropY,
          width: targetWidth,
          height: targetHeight,
        );
      } else {
        // Aspect-fit scaling onto a target-size canvas.
        final scaleX = targetWidth / originalImage.width;
        final scaleY = targetHeight / originalImage.height;
        final scale = scaleX < scaleY
            ? scaleX
            : scaleY; // Use the smaller scale factor.

        // Scale the image to fit the target size.
        final newWidth = (originalImage.width * scale).toInt();
        final newHeight = (originalImage.height * scale).toInt();
        final scaledImage = img.copyResize(
          originalImage,
          width: newWidth,
          height: newHeight,
        );

        // Draw the scaled image at the top-left of a target-size canvas.
        final canvas = img.Image(width: targetWidth, height: targetHeight);
        // Composite without color blending.
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

    // Compress to the requested memory budget by adjusting JPEG quality.
    if (params.memorySize == null) {
      // No size limit: encode at full quality.
      return img.encodeJpg(processedImage, quality: 100);
    }

    // Convert the limit from MB to bytes.
    final maxBytes = (params.memorySize! * 1024 * 1024).toInt();
    int quality = 100; // Matches iOS compressionQuality = 1.0.
    // Encode the initial JPEG.
    Uint8List compressedData = img.encodeJpg(processedImage, quality: quality);
    // Reduce quality by 30% per pass until the size limit is met or quality reaches 1%.
    while (compressedData.length > maxBytes && quality > 1) {
      quality = (quality * 0.7).round();
      if (quality < 1) quality = 1; // Never drop below 1% quality.
      compressedData = img.encodeJpg(processedImage, quality: quality);
    }

    return compressedData;
  } catch (e) {
    debugPrint('图片压缩Failed:$e');
    return null;
  }
}
