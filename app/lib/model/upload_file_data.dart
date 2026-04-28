/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

class UploadFile {
  final String? path;

  UploadFile({this.path});

  factory UploadFile.fromJson(Map<String, dynamic> json) {
    return UploadFile(path: json['path']);
  }
}
