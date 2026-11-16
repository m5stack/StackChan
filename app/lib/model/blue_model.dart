/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

import 'dart:convert';

class BlueWifi {
  String? ssid;
  String? password;

  /// Optional band hint so the firmware can tell a 5 GHz network apart from a
  /// missing one. Frequency is in MHz and is only known on some platforms.
  int? frequency;

  /// True when [ssid] is the Wi-Fi network this phone is currently connected to.
  bool? currentNetwork;

  BlueWifi({this.ssid, this.password, this.frequency, this.currentNetwork});

  Map<String, dynamic> toMap() {
    return {
      'ssid': ssid,
      'password': password,
      if (frequency != null) 'frequency': frequency,
      if (currentNetwork != null) 'currentNetwork': currentNetwork,
    };
  }

  factory BlueWifi.fromMap(Map<String, dynamic> map) {
    return BlueWifi(
      ssid: map['ssid'],
      password: map['password'],
      frequency: map['frequency'],
      currentNetwork: map['currentNetwork'],
    );
  }
}

class BlueEncryptionDecryption {
  String? cmd;
  String? data;

  // Creates an encrypted BLE message.
  BlueEncryptionDecryption({this.cmd, this.data});

  // Creates a message from a JSON map.
  BlueEncryptionDecryption.fromJson(Map<String, dynamic> json) {
    // Read each value with an explicit type to avoid type errors.
    cmd = json['cmd'] as String?;
    data = json['data'] as String?;
  }

  // Converts this message to a JSON map.
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

  // Compact, human-readable representation for logs.
  @override
  String toString() {
    return '{\n'
        '  cmd: $cmd,\n'
        '  data: $data\n'
        '}';
  }

  // Multi-line representation that is easier to read while debugging.
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
  String? errorCode;
  String? errorMessage;
  int? reason;
  int? sdkError;
  String? ssid;

  BlueNotifyState({
    this.type,
    this.state,
    this.errorCode,
    this.errorMessage,
    this.reason,
    this.sdkError,
    this.ssid,
  });

  String get wifiFailureDescription {
    var message = errorMessage?.trim() ?? '';
    if (message.isEmpty && state?.startsWith('wifiConnectFailed:') == true) {
      message = state!.substring('wifiConnectFailed:'.length).trim();
    }
    if (message.isEmpty) {
      message = 'Configuration failed. Check the Wi-Fi name, password and router range.';
    }
    if (reason != null && reason! > 0) {
      message += '\nWi-Fi reason: $reason';
    }
    if (sdkError != null && sdkError != 0) {
      message += '\nDevice error: $sdkError';
    }
    return message;
  }

  Map<String, dynamic> toMap() {
    return {
      'type': type,
      'state': state,
      if (errorCode != null) 'errorCode': errorCode,
      if (errorMessage != null) 'errorMessage': errorMessage,
      if (reason != null) 'reason': reason,
      if (sdkError != null) 'sdkError': sdkError,
      if (ssid != null) 'ssid': ssid,
    };
  }

  factory BlueNotifyState.fromMap(Map<String, dynamic> map) {
    return BlueNotifyState(
      type: map['type'],
      state: map['state'],
      errorCode: map['errorCode'],
      errorMessage: map['errorMessage'],
      reason: map['reason'],
      sdkError: map['sdkError'],
      ssid: map['ssid'],
    );
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
