/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

import 'dart:typed_data';

import 'package:stack_chan/model/msg_type.dart';
import 'package:stack_chan/network/local_area_network_service.dart';

typedef RemoteDeviceSender = void Function(MsgType type, {Uint8List? data});

/// Sends page messages over LAN, falling back to remote when LAN is unavailable, the target differs, or sending fails.
class DeviceMessageTransport {
  DeviceMessageTransport({required this.local, required this.remote});

  final LocalAreaNetworkService local;
  final RemoteDeviceSender remote;

  void send(MsgType type, {required String deviceMac, Uint8List? data}) {
    if (!local.sendConnectedMessage(
      type,
      deviceMac: deviceMac,
      payload: data,
    )) {
      remote(type, data: data);
    }
  }
}
