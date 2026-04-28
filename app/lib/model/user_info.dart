/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

class UserInfo {
  int? uid;
  String? username;
  String? userslug;
  String? displayName;
  String? iconText;
  String? iconBgColor;
  int? emailConfirmed;
  int? joinDate;
  int? lastOnline;
  String? userStatus;

  UserInfo({
    this.uid,
    this.username,
    this.userslug,
    this.displayName,
    this.iconText,
    this.iconBgColor,
    this.emailConfirmed,
    this.joinDate,
    this.lastOnline,
    this.userStatus,
  });

  factory UserInfo.fromJson(Map<String, dynamic> json) {
    return UserInfo(
      uid: json['uid'] as int?,
      username: json['username']?.toString(),
      userslug: json['userslug']?.toString(),
      displayName: json['displayName']?.toString(),
      iconText: json['iconText']?.toString(),
      iconBgColor: json['iconBgColor']?.toString(),
      emailConfirmed: json['emailConfirmed'] as int?,
      joinDate: json['joinDate'] as int?,
      lastOnline: json['lastOnline'] as int?,
      userStatus: json['userStatus']?.toString(),
    );
  }

  Map<String, dynamic> toJson() {
    return {
      'uid': uid,
      'username': username,
      'userslug': userslug,
      'displayName': displayName,
      'iconText': iconText,
      'iconBgColor': iconBgColor,
      'emailConfirmed': emailConfirmed,
      'joinDate': joinDate,
      'lastOnline': lastOnline,
      'userStatus': userStatus,
    };
  }
}
