/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

class RegistrationResponse {
  int? uid;
  String? username;
  String? userslug;
  String? email;
  int? emailConfirmed;
  int? joinDate;
  int? lastOnline;
  dynamic picture;
  String? iconBgColor;
  dynamic fullname;
  String? displayname;
  String? iconText;
  String? userStatus;

  RegistrationResponse({
    this.uid,
    this.username,
    this.userslug,
    this.email,
    this.emailConfirmed,
    this.joinDate,
    this.lastOnline,
    this.picture,
    this.iconBgColor,
    this.fullname,
    this.displayname,
    this.iconText,
    this.userStatus,
  });

  ///from JSON toobject
  RegistrationResponse.fromJson(Map<String, dynamic> json) {
    uid = json['uid'];
    username = json['username'];
    userslug = json['userslug'];
    email = json['email'];
    emailConfirmed = json['email:confirmed'];
    joinDate = json['joindate'];
    lastOnline = json['lastonline'];
    picture = json['picture'];
    iconBgColor = json['icon:bgColor'];
    fullname = json['fullname'];
    displayname = json['displayname'];
    iconText = json['icon:text'];
    userStatus = json['status'];
  }

  ///objectto JSON
  Map<String, dynamic> toJson() {
    final map = <String, dynamic>{};
    map['uid'] = uid;
    map['username'] = username;
    map['userslug'] = userslug;
    map['email'] = email;
    map['email:confirmed'] = emailConfirmed;
    map['joindate'] = joinDate;
    map['lastonline'] = lastOnline;
    map['picture'] = picture;
    map['icon:bgColor'] = iconBgColor;
    map['fullname'] = fullname;
    map['displayname'] = displayname;
    map['icon:text'] = iconText;
    map['status'] = userStatus;
    return map;
  }
}
