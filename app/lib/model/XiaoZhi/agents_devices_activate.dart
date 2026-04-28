/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/


import 'agent.dart';
import 'agents_device.dart';

class AgentsDevicesActivate {
  String? macAddress;
  String? serialNumber;
  int? agentId;
  Agent? agent;
  AgentsDevice? device;

  //Constructorfunction
  AgentsDevicesActivate({
    this.macAddress,
    this.serialNumber,
    this.agentId,
    this.agent,
    this.device,
  });

  //from JSON convert
  factory AgentsDevicesActivate.fromJson(Map<String, dynamic> json) {
    return AgentsDevicesActivate(
      macAddress: json['macAddress'],
      serialNumber: json['serialNumber'],
      agentId: json['agentId'],
      agent: json['agent'] != null ? Agent.fromJson(json['agent']) : null,
      device: json['device'] != null
          ? AgentsDevice.fromJson(json['device'])
          : null,
    );
  }

  //convertas JSON
  Map<String, dynamic> toJson() {
    final Map<String, dynamic> data = <String, dynamic>{};
    data['macAddress'] = macAddress;
    data['serialNumber'] = serialNumber;
    data['agentId'] = agentId;
    if (agent != null) {
      data['agent'] = agent!.toJson();
    }
    if (device != null) {
      data['device'] = device!.toJson();
    }
    return data;
  }
}