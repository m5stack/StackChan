/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

import 'package:uuid/v4.dart';

class Device {
  String mac;
  String? name;
  int? uid;
  String? bindTime;

  Device({required this.mac, this.name, this.uid, this.bindTime});

  String getDisplayName() {
    if (name != null && name != "") {
      return name!;
    } else {
      return mac;
    }
  }

  Device.fromJson(Map<String, dynamic> json)
    : mac = json['mac'] ?? UuidV4().toString(),
      uid = json['uid'] as int?,
      name = json['name']?.toString(),
      bindTime = json["bind_time"]?.toString();

  Map<String, dynamic> toJson() {
    return {'mac': mac, 'name': name, 'uid': uid, 'bind_time': bindTime};
  }

  static List<Device> fromListJson(List<dynamic> list) {
    return list.map((i) => Device.fromJson(i as Map<String, dynamic>)).toList();
  }
}
