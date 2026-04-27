/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

class AgentsDevice {
  int? id;
  int? user_id;
  String? mac_address;
  String? created_at;
  String? updated_at;
  String? last_connected_at;
  int? auto_update;
  dynamic board;
  String? alias;
  String? agent_code;
  int? agent_id;
  String? app_version;
  int? is_deleted;
  String? board_name;
  String? serial_number;

  //Constructorfunction
  AgentsDevice({
    this.id,
    this.user_id,
    this.mac_address,
    this.created_at,
    this.updated_at,
    this.last_connected_at,
    this.auto_update,
    this.board,
    this.alias,
    this.agent_code,
    this.agent_id,
    this.app_version,
    this.is_deleted,
    this.board_name,
    this.serial_number,
  });

  //from JSON convert
  factory AgentsDevice.fromJson(Map<String, dynamic> json) {
    return AgentsDevice(
      id: json['id'],
      user_id: json['user_id'],
      mac_address: json['mac_address'],
      created_at: json['created_at'],
      updated_at: json['updated_at'],
      last_connected_at: json['last_connected_at'],
      auto_update: json['auto_update'],
      board: json['board'],
      alias: json['alias'],
      agent_code: json['agent_code'],
      agent_id: json['agent_id'],
      app_version: json['app_version'],
      is_deleted: json['is_deleted'],
      board_name: json['board_name'],
      serial_number: json['serial_number'],
    );
  }

  //convertas JSON
  Map<String, dynamic> toJson() {
    final Map<String, dynamic> data = <String, dynamic>{};
    data['id'] = id;
    data['user_id'] = user_id;
    data['mac_address'] = mac_address;
    data['created_at'] = created_at;
    data['updated_at'] = updated_at;
    data['last_connected_at'] = last_connected_at;
    data['auto_update'] = auto_update;
    data['board'] = board;
    data['alias'] = alias;
    data['agent_code'] = agent_code;
    data['agent_id'] = agent_id;
    data['app_version'] = app_version;
    data['is_deleted'] = is_deleted;
    data['board_name'] = board_name;
    data['serial_number'] = serial_number;
    return data;
  }
}
