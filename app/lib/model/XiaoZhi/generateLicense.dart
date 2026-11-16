/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

class GenerateLicense {
  // Dart-style field names; the API uses snake_case.
  String? productName;
  String? boardName;
  String? serialNumber;
  String? licenseKey;
  String? licenseAlgorithm;
  String? createdAt;
  dynamic firmware;

  // Creates a license payload with every field optional.
  GenerateLicense({
    this.productName,
    this.boardName,
    this.serialNumber,
    this.licenseKey,
    this.licenseAlgorithm,
    this.createdAt,
    this.firmware,
  });

  // Creates a license payload from a JSON map.
  factory GenerateLicense.fromJson(Map<String, dynamic> json) {
    return GenerateLicense(
      // Map the API's snake_case fields to Dart fields.
      productName: json['product_name'] as String?,
      boardName: json['board_name'] as String?,
      serialNumber: json['serial_number'] as String?,
      licenseKey: json['license_key'] as String?,
      licenseAlgorithm: json['license_algorithm'] as String?,
      createdAt: json['created_at'] as String?,
      firmware: json['firmware'], // Kept dynamic; the shape varies by product.
    );
  }

  // Serializes the payload for local storage or later use.
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
