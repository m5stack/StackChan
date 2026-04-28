/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

///licenseentityClass


class License {
  int? id;
  int? productId;
  String? serialNumber;
  String? licenseAlgorithm;
  String? licenseKey;
  String? status;
  String? createdAt;
  String? updatedAt;
  dynamic activateAt;
  String? seed;
  String? billingType;
  dynamic macAddress;

  License({
    this.id,
    this.productId,
    this.serialNumber,
    this.licenseAlgorithm,
    this.licenseKey,
    this.status,
    this.createdAt,
    this.updatedAt,
    this.activateAt,
    this.seed,
    this.billingType,
    this.macAddress,
  });

  ///JSON to License object
  factory License.fromJson(Map<String, dynamic> json) => License(
    id: json['id'] as int?,
    productId: json['product_id'] as int?,
    serialNumber: json['serial_number'] as String?,
    licenseAlgorithm: json['license_algorithm'] as String?,
    licenseKey: json['license_key'] as String?,
    status: json['status'] as String?,
    createdAt: json['created_at'] as String?,
    updatedAt: json['updated_at'] as String?,
    activateAt: json['activate_at'],
    seed: json['seed'] as String?,
    billingType: json['billing_type'] as String?,
    macAddress: json['mac_address'],
  );

  ///objectto JSON
  Map<String, dynamic> toJson() => {
    'id': id,
    'product_id': productId,
    'serial_number': serialNumber,
    'license_algorithm': licenseAlgorithm,
    'license_key': licenseKey,
    'status': status,
    'created_at': createdAt,
    'updated_at': updatedAt,
    'activate_at': activateAt,
    'seed': seed,
    'billing_type': billingType,
    'mac_address': macAddress,
  };
}
