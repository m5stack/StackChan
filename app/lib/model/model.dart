/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

import 'dart:convert';

class Model<T> {
  int? code;
  String? message;
  T? data;

  Model.fromJson(Map<String, dynamic> map, {T Function(dynamic)? factory}) {
    if (map["code"] != null && map["code"] != "null") {
      code = map["code"];
    }
    if (map["data"] != null && map["data"] != "null" && factory != null) {
      data = factory(map["data"]);
    } else {
      data = map["data"];
    }
    if (map["message"] != null && map["message"] != "null") {
      message = map["message"];
    }
  }

  Model.fromJsonT(dynamic data, {T Function(dynamic)? factory})
    : this.fromJson(data is String ? jsonDecode(data) : data, factory: factory);

  Model.fromJsonString(String? jsonString, {T Function(dynamic)? factory})
    : this.fromJson(jsonDecode(jsonString ?? ""), factory: factory);

  bool isSuccess() => code == 0;
}
