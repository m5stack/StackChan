// SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
// SPDX-License-Identifier: MIT

import 'dart:convert';

class ExpressionData {
  String type;
  ExpressionItem leftEye;
  ExpressionItem rightEye;
  ExpressionItem mouth;

  ExpressionData({
    this.type = "bleAvatar",
    required this.leftEye,
    required this.rightEye,
    required this.mouth,
  });

  factory ExpressionData.fromJson(Map<String, dynamic> json) => ExpressionData(
    type: json['type'] ?? "bleAvatar",
    leftEye: ExpressionItem.fromJson(json['leftEye']),
    rightEye: ExpressionItem.fromJson(json['rightEye']),
    mouth: ExpressionItem.fromJson(json['mouth']),
  );

  @override
  String toString() {
    return jsonEncode(toJson());
  }

  Map<String, dynamic> toJson() => {
    'type': type,
    'leftEye': leftEye.toJson(),
    'rightEye': rightEye.toJson(),
    'mouth': mouth.toJson(),
  };
}

class ExpressionItem {
  int x;
  int y;
  int rotation;
  int weight;
  int size;

  ExpressionItem({
    this.x = 0,
    this.y = 0,
    this.rotation = 0,
    this.weight = 0,
    this.size = 0,
  });

  ExpressionItem copy() => ExpressionItem(
    x: x,
    y: y,
    rotation: rotation,
    weight: weight,
    size: size,
  );

  factory ExpressionItem.fromJson(Map<String, dynamic> json) => ExpressionItem(
    x: json['x'] ?? 0,
    y: json['y'] ?? 0,
    rotation: json['rotation'] ?? 0,
    weight: json['weight'] ?? 0,
    size: json['size'] ?? 0,
  );

  Map<String, dynamic> toJson() => {
    'x': x,
    'y': y,
    'rotation': rotation,
    'weight': weight,
    'size': size,
  };
}

class MotionData {
  String type;
  MotionDataItem pitchServo;
  MotionDataItem yawServo;

  MotionData({
    this.type = "bleMotion",
    required this.pitchServo,
    required this.yawServo,
  });

  @override
  String toString() {
    return jsonEncode(toJson());
  }

  factory MotionData.fromJson(Map<String, dynamic> json) => MotionData(
    type: json['type'] ?? "bleMotion",
    pitchServo: MotionDataItem.fromJson(json['pitchServo']),
    yawServo: MotionDataItem.fromJson(json['yawServo']),
  );

  Map<String, dynamic> toJson() => {
    'type': type,
    'pitchServo': pitchServo.toJson(),
    'yawServo': yawServo.toJson(),
  };
}

class MotionDataItem {
  int angle;
  int speed;
  int rotate;

  MotionDataItem({this.angle = 0, this.speed = 500, this.rotate = 0});

  MotionDataItem copy() =>
      MotionDataItem(angle: angle, speed: speed, rotate: rotate);

  factory MotionDataItem.fromJson(Map<String, dynamic> json) {
    return MotionDataItem(
      angle: json['angle'] ?? 0,
      speed: json['speed'] ?? 500,
      rotate: json['rotate'] ?? 0,
    );
  }

  Map<String, dynamic> toJson() {
    if (angle != 0) {
      return {'angle': angle, 'speed': speed};
    } else if (rotate != 0) {
      return {'rotate': rotate, 'speed': speed};
    } else {
      return {'angle': angle, 'speed': speed};
    }
  }
}

class RgbData {
  String? leftRgbColor = "#FFFFFF";
  double? leftRgbDuration = 0.0;
  String? rightRgbColor = "#FFFFFF";
  double? rightRgbDuration = 0.0;

  RgbData({
    this.leftRgbColor,
    this.leftRgbDuration,
    this.rightRgbColor,
    this.rightRgbDuration,
  });

  RgbData.fromJson(Map<String, dynamic> json) {
    leftRgbColor = json['leftRgbColor'] ?? "#FFFFFF";
    leftRgbDuration = (json['leftRgbDuration'] ?? 0.0).toDouble();
    rightRgbColor = json['rightRgbColor'] ?? "#FFFFFF";
    rightRgbDuration = (json['rightRgbDuration'] ?? 0.0).toDouble();
  }

  //Serializeas JSON
  Map<String, dynamic> toJson() {
    final Map<String, dynamic> data = <String, dynamic>{};
    data['leftRgbColor'] = leftRgbColor;
    data['leftRgbDuration'] = leftRgbDuration;
    data['rightRgbColor'] = rightRgbColor;
    data['rightRgbDuration'] = rightRgbDuration;
    return data;
  }

  @override
  String toString() {
    return jsonEncode(toJson());
  }
}
