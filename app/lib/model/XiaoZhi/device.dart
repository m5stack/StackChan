/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

class Device {
  int? deviceId;
  int? agentId;
  int? id;
  int? productId;
  String? seed;
  String? serialNumber;
  String? activateAt;
  String? productName;
  String? macAddress;
  String? appVersion;
  String? boardName;
  String? clientId;
  String? iccid;
  String? imei;
  bool? online;
  int? autoUpdate;
  String? alias;

  // Creates a device with every field optional.
  Device({
    this.deviceId,
    this.agentId,
    this.id,
    this.productId,
    this.seed,
    this.serialNumber,
    this.activateAt,
    this.productName,
    this.macAddress,
    this.appVersion,
    this.boardName,
    this.clientId,
    this.iccid,
    this.imei,
    this.autoUpdate,
    this.alias,
    this.online,
  });

  // Creates a device from a JSON map.
  factory Device.fromJson(Map<String, dynamic> json) {
    return Device(
      deviceId: json['device_id'] as int?,
      agentId: json['agent_id'] as int?,
      id: json['id'] as int?,
      productId: json['product_id'] as int?,
      seed: json['seed'] as String?,
      serialNumber: json['serial_number'] as String?,
      activateAt: json['activate_at'] as String?,
      productName: json['product_name'] as String?,
      macAddress: json['mac_address'] as String?,
      appVersion: json['app_version'] as String?,
      boardName: json['board_name'] as String?,
      clientId: json['client_id'] as String?,
      iccid: json['iccid'] as String?,
      imei: json['imei'] as String?,
      autoUpdate: json['auto_update'] as int?,
      alias: json['alias']?.toString(),
      online: json['online'] as bool?,
    );
  }

  // Converts this device to a JSON map, omitting unset fields.
  Map<String, dynamic> toJson() {
    final Map<String, dynamic> data = <String, dynamic>{};
    if (deviceId != null) data['device_id'] = deviceId;
    if (agentId != null) data['agent_id'] = agentId;
    if (id != null) data['id'] = id;
    if (productId != null) data['product_id'] = productId;
    if (seed != null) data['seed'] = seed;
    if (serialNumber != null) data['serial_number'] = serialNumber;
    if (activateAt != null) data['activate_at'] = activateAt;
    if (productName != null) data['product_name'] = productName;
    if (macAddress != null) data['mac_address'] = macAddress;
    if (appVersion != null) data['app_version'] = appVersion;
    if (boardName != null) data['board_name'] = boardName;
    if (clientId != null) data['client_id'] = clientId;
    if (iccid != null) data['iccid'] = iccid;
    if (imei != null) data['imei'] = imei;
    if (autoUpdate != null) data['auto_update'] = autoUpdate;
    if (alias != null) data['alias'] = alias;
    if (online != null) data['online'] = online;
    return data;
  }
}

class LastDevice {
  int? id;
  int? userId;
  String? macAddress;
  String? createdAt;
  String? updatedAt;
  String? lastConnectedAt;
  int? autoUpdate;
  String? alias;
  int? agentId;

  // Creates a record of the last connected device; all fields are optional.
  LastDevice({
    this.id,
    this.userId,
    this.macAddress,
    this.createdAt,
    this.updatedAt,
    this.lastConnectedAt,
    this.autoUpdate,
    this.alias,
    this.agentId,
  });

  // Creates a record from a JSON map.
  factory LastDevice.fromJson(Map<String, dynamic> json) {
    return LastDevice(
      id: json['id'] as int?,
      userId: json['user_id'] as int?,
      macAddress: json['mac_address'] as String?,
      createdAt: json['created_at'] as String?,
      updatedAt: json['updated_at'] as String?,
      lastConnectedAt: json['last_connected_at'] as String?,
      autoUpdate: json['auto_update'] as int?,
      alias: json['alias'] as String?,
      agentId: json['agent_id'] as int?,
    );
  }

  // Converts this record to a JSON map.
  Map<String, dynamic> toJson() {
    final Map<String, dynamic> data = <String, dynamic>{};
    // Null fields are serialized as null, which is valid JSON.
    data['id'] = id;
    data['user_id'] = userId;
    data['mac_address'] = macAddress;
    data['created_at'] = createdAt;
    data['updated_at'] = updatedAt;
    data['last_connected_at'] = lastConnectedAt;
    data['auto_update'] = autoUpdate;
    data['alias'] = alias;
    data['agent_id'] = agentId;
    return data;
  }
}
