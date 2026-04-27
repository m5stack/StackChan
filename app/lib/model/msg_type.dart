// SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
//
// SPDX-License-Identifier: MIT

enum MsgType {
  opus(0x01),
  jpeg(0x02),
  controlAvatar(0x03),
  controlMotion(0x04),
  onCamera(0x05),
  offCamera(0x06),
  textMessage(0x07),
  requestCall(0x09),
  refuseCall(0x0A),
  agreeCall(0x0B),
  hangupCall(0x0C),
  updateDeviceName(0x0D),
  getDeviceName(0x0E),
  ping(0x10),
  pong(0x11),
  onPhoneScreen(0x12),
  offPhoneScreen(0x13),
  dance(0x14),
  getAvatarPosture(0x15),
  deviceOffline(0x16),
  deviceOnline(0x17),
  onAudio(0x18),
  offAudio(0x19),
  aimedTakePhoto(0x1A);

  final int value; //custom值，andiOSrawValuefullyfor齐

  const MsgType(this.value);

  //修CurrentlySerializelogic:用valueAnd / WhileNon-index
  String toJson() => value.toString();

  static MsgType fromJson(String json) {
    final int value = int.parse(json);
    return MsgType.values.firstWhere(
      (e) => e.value == value,
      orElse: () => throw ArgumentError('Invalid MsgType value: $json'),
    );
  }
}
