/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

import 'package:stack_chan/util/music_util.dart';
import 'package:uuid/uuid.dart';

import 'expression_data.dart';

const _uuid = Uuid();

class DanceList {
  List<DanceData> danceData = [];
  int? danceIndex;
  String? danceName;
  int? id;
  String? musicUrl;

  MusicInfo? musicInfo;

  bool isLoading = false;

  DanceList({
    this.danceData = const [],
    this.danceIndex,
    this.danceName,
    this.musicUrl,
    this.id,
  });

  factory DanceList.fromJson(Map<String, dynamic> json) {
    return DanceList(
      danceData: json['danceData'] != null
          ? (json['danceData'] as List)
                .map((e) => DanceData.fromJson(e))
                .toList()
          : [],
      danceIndex: json['danceIndex'],
      danceName: json['danceName'],
      id: json['id'],
      musicUrl: json['musicUrl'],
    );
  }

  static List<DanceList> fromListJson(List<dynamic> jsonList) {
    if (jsonList.isEmpty) return [];
    return jsonList.map((json) => DanceList.fromJson(json)).toList();
  }

  List<Map<String, dynamic>> danceDataToJson() {
    if (danceData.isEmpty) {
      return [];
    }
    return danceData.map((danceDataItem) => danceDataItem.toJson()).toList();
  }

  Map<String, dynamic> toJson() {
    return {
      'danceData': danceData.map((e) => e.toJson()).toList(),
      'danceIndex': danceIndex,
      'danceName': danceName,
      'id': id,
      'musicUrl': musicUrl,
    };
  }
}

class DanceData {
  ExpressionItem leftEye; // default weight = 100
  ExpressionItem rightEye; // default weight = 100
  ExpressionItem mouth; // default weight = 0
  MotionDataItem yawServo; // (-1280 ~ 1280)
  MotionDataItem pitchServo; // (0 ~ 900)

  String leftRgbColor;
  String rightRgbColor;

  int durationMs;
  String id;

  static DanceData init() {
    return DanceData(
      leftEye: ExpressionItem(weight: 100),
      rightEye: ExpressionItem(weight: 100),
      mouth: ExpressionItem(weight: 0),
      yawServo: MotionDataItem(angle: 0),
      pitchServo: MotionDataItem(angle: 0),
      durationMs: 1000,
    );
  }

  DanceData({
    required this.leftEye,
    required this.rightEye,
    required this.mouth,
    required this.yawServo,
    required this.pitchServo,
    this.leftRgbColor = "#000000",
    this.rightRgbColor = "#000000",
    required this.durationMs,
    String? id,
  }) : id = id ?? _uuid.v4();

  ///等价 Swift init(from decoder:)
  factory DanceData.fromJson(Map<String, dynamic> json) {
    return DanceData(
      leftEye: ExpressionItem.fromJson(json['leftEye']),
      rightEye: ExpressionItem.fromJson(json['rightEye']),
      mouth: ExpressionItem.fromJson(json['mouth']),
      yawServo: MotionDataItem.fromJson(json['yawServo']),
      pitchServo: MotionDataItem.fromJson(json['pitchServo']),
      leftRgbColor: json['leftRgbColor'] ?? "#000000",
      rightRgbColor: json['rightRgbColor'] ?? "#000000",
      durationMs: json['durationMs'],
    );
  }

  Map<String, dynamic> toJson() {
    return {
      'leftEye': leftEye.toJson(),
      'rightEye': rightEye.toJson(),
      'mouth': mouth.toJson(),
      'yawServo': yawServo.toJson(),
      'pitchServo': pitchServo.toJson(),
      'leftRgbColor': leftRgbColor,
      'rightRgbColor': rightRgbColor,
      'durationMs': durationMs,
    };
  }

  ///等价 Swift  copy()
  DanceData copy() {
    return DanceData(
      leftEye: leftEye.copy(),
      rightEye: rightEye.copy(),
      mouth: mouth.copy(),
      yawServo: yawServo.copy(),
      pitchServo: pitchServo.copy(),
      leftRgbColor: leftRgbColor,
      rightRgbColor: rightRgbColor,
      durationMs: durationMs,
    );
  }

  static List<Map<String, dynamic>> listToJson(List<DanceData> list) {
    if (list.isEmpty) return [];
    return list.map((danceData) => danceData.toJson()).toList();
  }
}
