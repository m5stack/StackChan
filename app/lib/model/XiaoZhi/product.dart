/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

///deviceinfoentityClass
class Product {
  int? id;
  int? developerId;
  String? boardName;
  String? productType;
  String? productName;
  String? productDescription;
  String? serialNumberPrefix;
  String? licenseAlgorithm;
  String? createdAt;
  String? updatedAt;
  McpConfig? mcpConfig;
  String? remark;
  int? latestFirmwareId;
  int? testingFirmwareId;
  int? licenseType;
  String? voiceCloneStatus;
  int? agentTemplateId;
  dynamic hotwords;
  String? xiaozhiVersion;
  String? payMethod;
  dynamic asrModel;
  int? toolConciseMode;

  Product({
    this.id,
    this.developerId,
    this.boardName,
    this.productType,
    this.productName,
    this.productDescription,
    this.serialNumberPrefix,
    this.licenseAlgorithm,
    this.createdAt,
    this.updatedAt,
    this.mcpConfig,
    this.remark,
    this.latestFirmwareId,
    this.testingFirmwareId,
    this.licenseType,
    this.voiceCloneStatus,
    this.agentTemplateId,
    this.hotwords,
    this.xiaozhiVersion,
    this.payMethod,
    this.asrModel,
    this.toolConciseMode,
  });

  ///JSON toobject
  factory Product.fromJson(Map<String, dynamic> json) => Product(
    id: json['id'] as int?,
    developerId: json['developer_id'] as int?,
    boardName: json['board_name'] as String?,
    productType: json['product_type'] as String?,
    productName: json['product_name'] as String?,
    productDescription: json['product_description'] as String?,
    serialNumberPrefix: json['serial_number_prefix'] as String?,
    licenseAlgorithm: json['license_algorithm'] as String?,
    createdAt: json['created_at'] as String?,
    updatedAt: json['updated_at'] as String?,
    mcpConfig: json['mcp_config'] == null
        ? null
        : McpConfig.fromJson(json['mcp_config'] as Map<String, dynamic>),
    remark: json['remark'],
    latestFirmwareId: json['latest_firmware_id'] as int?,
    testingFirmwareId: json['testing_firmware_id'],
    licenseType: json['license_type'] as int?,
    voiceCloneStatus: json['voice_clone_status'] as String?,
    agentTemplateId: json['agent_template_id'] as int?,
    hotwords: json['hotwords'],
    xiaozhiVersion: json['xiaozhi_version'] as String?,
    payMethod: json['pay_method'] as String?,
    asrModel: json['asr_model'],
    toolConciseMode: json['tool_concise_mode'] as int?,
  );

  ///objectto JSON
  Map<String, dynamic> toJson() => {
    'id': id,
    'developer_id': developerId,
    'board_name': boardName,
    'product_type': productType,
    'product_name': productName,
    'product_description': productDescription,
    'serial_number_prefix': serialNumberPrefix,
    'license_algorithm': licenseAlgorithm,
    'created_at': createdAt,
    'updated_at': updatedAt,
    'mcp_config': mcpConfig?.toJson(),
    'remark': remark,
    'latest_firmware_id': latestFirmwareId,
    'testing_firmware_id': testingFirmwareId,
    'license_type': licenseType,
    'voice_clone_status': voiceCloneStatus,
    'agent_template_id': agentTemplateId,
    'hotwords': hotwords,
    'xiaozhi_version': xiaozhiVersion,
    'pay_method': payMethod,
    'asr_model': asrModel,
    'tool_concise_mode': toolConciseMode,
  };
}

///MCP configentityClass
class McpConfig {
  List<dynamic>? endpointIds;

  McpConfig({this.endpointIds});

  factory McpConfig.fromJson(Map<String, dynamic> json) =>
      McpConfig(endpointIds: json['endpoint_ids'] as List<dynamic>?);

  Map<String, dynamic> toJson() => {'endpoint_ids': endpointIds};
}
