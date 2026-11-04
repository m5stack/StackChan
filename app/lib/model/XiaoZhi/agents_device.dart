/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

class AgentsDevice {
  int? id;
  int? userId;
  String? macAddress;
  String? createdAt;
  String? updatedAt;
  String? lastConnectedAt;
  int? autoUpdate;
  dynamic board;
  String? alias;
  String? agentCode;
  int? agentId;
  String? appVersion;
  int? isDeleted;
  String? boardName;
  String? serialNumber;

  // Creates a device record; every field is optional.
  AgentsDevice({
    this.id,
    this.userId,
    this.macAddress,
    this.createdAt,
    this.updatedAt,
    this.lastConnectedAt,
    this.autoUpdate,
    this.board,
    this.alias,
    this.agentCode,
    this.agentId,
    this.appVersion,
    this.isDeleted,
    this.boardName,
    this.serialNumber,
  });

  // Creates a device record from a JSON map.
  factory AgentsDevice.fromJson(Map<String, dynamic> json) {
    return AgentsDevice(
      id: json['id'],
      userId: json['user_id'],
      macAddress: json['mac_address'],
      createdAt: json['created_at'],
      updatedAt: json['updated_at'],
      lastConnectedAt: json['last_connected_at'],
      autoUpdate: json['auto_update'],
      board: json['board'],
      alias: json['alias'],
      agentCode: json['agent_code'],
      agentId: json['agent_id'],
      appVersion: json['app_version'],
      isDeleted: json['is_deleted'],
      boardName: json['board_name'],
      serialNumber: json['serial_number'],
    );
  }

  // Converts this device record to a JSON map.
  Map<String, dynamic> toJson() {
    final Map<String, dynamic> data = <String, dynamic>{};
    data['id'] = id;
    data['user_id'] = userId;
    data['mac_address'] = macAddress;
    data['created_at'] = createdAt;
    data['updated_at'] = updatedAt;
    data['last_connected_at'] = lastConnectedAt;
    data['auto_update'] = autoUpdate;
    data['board'] = board;
    data['alias'] = alias;
    data['agent_code'] = agentCode;
    data['agent_id'] = agentId;
    data['app_version'] = appVersion;
    data['is_deleted'] = isDeleted;
    data['board_name'] = boardName;
    data['serial_number'] = serialNumber;
    return data;
  }
}
