/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

import 'package:flutter_blue_plus/flutter_blue_plus.dart';

class BlueDeviceInfo {
  final BluetoothDevice device;
  final Map<String, dynamic> advertisementData;
  final int rssi;
  DateTime lastSeen;

  BlueDeviceInfo({
    required this.device,
    required this.advertisementData,
    required this.rssi,
    required this.lastSeen,
  });

  @override
  bool operator ==(Object other) =>
      identical(this, other) ||
      other is BlueDeviceInfo &&
          runtimeType == other.runtimeType &&
          device.remoteId == other.device.remoteId;

  @override
  int get hashCode => device.remoteId.hashCode;
}
