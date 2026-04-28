/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

import 'dart:convert';

class BlueWifi {
  String? ssid;
  String? password;

  BlueWifi({this.ssid, this.password});

  Map<String, dynamic> toMap() {
    return {'ssid': ssid, 'password': password};
  }

  factory BlueWifi.fromMap(Map<String, dynamic> map) {
    return BlueWifi(ssid: map['ssid'], password: map['password']);
  }
}

class BlueEncryptionDecryption {
  String? cmd;
  String? data;

  //defaultConstructorfunction
  BlueEncryptionDecryption({this.cmd, this.data});

  //fromJson Constructorfunction:from JSON mapcreateinstance
  BlueEncryptionDecryption.fromJson(Map<String, dynamic> json) {
    //safefrom JSON inValue,avoidtypeerror
    cmd = json['cmd'] as String?;
    data = json['data'] as String?;
  }

  //optional:toJson method(Convenientwillobjecttoas JSON)
  Map<String, dynamic> toJson() {
    final Map<String, dynamic> json = <String, dynamic>{};
    if (cmd != null) {
      json['cmd'] = cmd;
    }
    if (data != null) {
      json['data'] = data;
    }
    return json;
  }

  //toString method:Stringoutput
  @override
  String toString() {
    return '{\n'
        '  cmd: $cmd,\n'
        '  data: $data\n'
        '}';
  }

  //optional:enhance toString(output,Debug)
  String toStringFormatted() {
    return 'BlueEncryptionDecryption {\n'
        '  cmd: "${cmd ?? 'null'}"\n'
        '  data: "${data ?? 'null'}"\n'
        '}';
  }
}

class BlueWifiModel {
  String? cmd;
  BlueWifi? data;

  BlueWifiModel({this.cmd, this.data});

  Map<String, dynamic> toMap() {
    return {'cmd': cmd, 'data': data?.toMap()};
  }

  String? toJson() {
    try {
      return const JsonEncoder.withIndent('  ').convert(toMap());
    } catch (_) {
      return null;
    }
  }

  static BlueWifiModel? fromJson(String json) {
    try {
      final map = jsonDecode(json) as Map<String, dynamic>;
      return BlueWifiModel(
        cmd: map['cmd'],
        data: map['data'] != null ? BlueWifi.fromMap(map['data']) : null,
      );
    } catch (_) {
      return null;
    }
  }
}

class BlueNotifyState {
  int? type;
  String? state;

  BlueNotifyState({this.type, this.state});

  Map<String, dynamic> toMap() {
    return {'type': type, 'state': state};
  }

  factory BlueNotifyState.fromMap(Map<String, dynamic> map) {
    return BlueNotifyState(type: map['type'], state: map['state']);
  }
}

class BlueNotifyStateModel {
  String? cmd;
  BlueNotifyState? data;

  BlueNotifyStateModel({this.cmd, this.data});

  Map<String, dynamic> toMap() {
    return {'cmd': cmd, 'data': data?.toMap()};
  }

  String? toJson() {
    try {
      return const JsonEncoder.withIndent('  ').convert(toMap());
    } catch (_) {
      return null;
    }
  }

  static BlueNotifyStateModel? fromJson(String json) {
    try {
      final map = jsonDecode(json) as Map<String, dynamic>;
      return BlueNotifyStateModel(
        cmd: map['cmd'],
        data: map['data'] != null ? BlueNotifyState.fromMap(map['data']) : null,
      );
    } catch (_) {
      return null;
    }
  }
}
