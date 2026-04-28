/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

class Device {
  int? device_id;
  int? agent_id;
  int? id;
  int? product_id;
  String? seed;
  String? serial_number;
  String? activate_at;
  String? product_name;
  String? mac_address;
  String? app_version;
  String? board_name;
  String? client_id;
  String? iccid;
  String? imei;
  bool? online;

  //Constructorfunction
  Device({
    this.device_id,
    this.agent_id,
    this.id,
    this.product_id,
    this.seed,
    this.serial_number,
    this.activate_at,
    this.product_name,
    this.mac_address,
    this.app_version,
    this.board_name,
    this.client_id,
    this.iccid,
    this.imei,
    this.online,
  });

  //fromJSONparse
  factory Device.fromJson(Map<String, dynamic> json) {
    return Device(
      device_id: json['device_id'] as int?,
      agent_id: json['agent_id'] as int?,
      id: json['id'] as int?,
      product_id: json['product_id'] as int?,
      seed: json['seed'] as String?,
      serial_number: json['serial_number'] as String?,
      activate_at: json['activate_at'] as String?,
      product_name: json['product_name'] as String?,
      mac_address: json['mac_address'] as String?,
      app_version: json['app_version'] as String?,
      board_name: json['board_name'] as String?,
      client_id: json['client_id'] as String?,
      iccid: json['iccid'] as String?,
      imei: json['imei'] as String?,
      online: json['online'] as bool?,
    );
  }

  //convertasJSON
  Map<String, dynamic> toJson() {
    final Map<String, dynamic> data = <String, dynamic>{};
    if (device_id != null) data['device_id'] = device_id;
    if (agent_id != null) data['agent_id'] = agent_id;
    if (id != null) data['id'] = id;
    if (product_id != null) data['product_id'] = product_id;
    if (seed != null) data['seed'] = seed;
    if (serial_number != null) data['serial_number'] = serial_number;
    if (activate_at != null) data['activate_at'] = activate_at;
    if (product_name != null) data['product_name'] = product_name;
    if (mac_address != null) data['mac_address'] = mac_address;
    if (app_version != null) data['app_version'] = app_version;
    if (board_name != null) data['board_name'] = board_name;
    if (client_id != null) data['client_id'] = client_id;
    if (iccid != null) data['iccid'] = iccid;
    if (imei != null) data['imei'] = imei;
    if (online != null) data['online'] = online;
    return data;
  }
}

class LastDevice {
  int? id;
  int? user_id;
  String? mac_address;
  String? created_at;
  String? updated_at;
  String? last_connected_at;
  int? auto_update;
  String? alias;
  int? agent_id;

  //Constructorfunction(allfieldasoptionalparameter,Nullsafe)
  LastDevice({
    this.id,
    this.user_id,
    this.mac_address,
    this.created_at,
    this.updated_at,
    this.last_connected_at,
    this.auto_update,
    this.alias,
    this.agent_id,
  });

  //from JSON Deserialize(factory Factorymethod,)
  factory LastDevice.fromJson(Map<String, dynamic> json) {
    return LastDevice(
      id: json['id'] as int?,
      user_id: json['user_id'] as int?,
      mac_address: json['mac_address'] as String?,
      created_at: json['created_at'] as String?,
      updated_at: json['updated_at'] as String?,
      last_connected_at: json['last_connected_at'] as String?,
      auto_update: json['auto_update'] as int?,
      alias: json['alias'] as String?,
      agent_id: json['agent_id'] as int?,
    );
  }

  //Serializeas JSON(return Map<String, dynamic>,Candirectfor jsonEncode)
  Map<String, dynamic> toJson() {
    final Map<String, dynamic> data = <String, dynamic>{};
    //one by onemapfield,nullWillautoSerializeas null( JSON standard)
    data['id'] = id;
    data['user_id'] = user_id;
    data['mac_address'] = mac_address;
    data['created_at'] = created_at;
    data['updated_at'] = updated_at;
    data['last_connected_at'] = last_connected_at;
    data['auto_update'] = auto_update;
    data['alias'] = alias;
    data['agent_id'] = agent_id;
    return data;
  }
}
