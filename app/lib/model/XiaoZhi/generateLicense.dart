/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

class GenerateLicense {
  //field采用Dart小驼峰namingstandard,parsewhenmapAPIreturnDown划线naming
  String? productName;
  String? boardName;
  String? serialNumber;
  String? licenseKey;
  String? licenseAlgorithm;
  String? createdAt;
  dynamic firmware;

  //defaultConstructorfunction
  GenerateLicense({
    this.productName,
    this.boardName,
    this.serialNumber,
    this.licenseKey,
    this.licenseAlgorithm,
    this.createdAt,
    this.firmware,
  });

  //core:fromJsonFactorymethod,parseJSONdatatoobject
  factory GenerateLicense.fromJson(Map<String, dynamic> json) {
    return GenerateLicense(
      //mapAPIreturnDown划线fieldtoDart小驼峰field
      productName: json['product_name'] as String?,
      boardName: json['board_name'] as String?,
      serialNumber: json['serial_number'] as String?,
      licenseKey: json['license_key'] as String?,
      licenseAlgorithm: json['license_algorithm'] as String?,
      createdAt: json['created_at'] as String?,
      firmware: json['firmware'], //dynamictypedirect赋值
    );
  }

  //optional:addtoJsonmethod(For easylaterSerialize,如Local存储)
  Map<String, dynamic> toJson() {
    return {
      'product_name': productName,
      'board_name': boardName,
      'serial_number': serialNumber,
      'license_key': licenseKey,
      'license_algorithm': licenseAlgorithm,
      'created_at': createdAt,
      'firmware': firmware,
    };
  }
}
