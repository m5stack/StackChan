/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

class McpEndpoints {
  int? id;
  int? developerId; //修正namingstandard：snake_case 转 camelCase
  String? name;
  String? description;
  int? enabled;
  String? createdAt; //修正namingstandard：snake_case 转 camelCase
  String? updatedAt; //修正namingstandard：snake_case 转 camelCase

  //defaultConstructorfunction
  McpEndpoints({
    this.id,
    this.developerId,
    this.name,
    this.description,
    this.enabled,
    this.createdAt,
    this.updatedAt,
  });

  ///fromsingle JSON objectconvertas McpEndpoints instance
  factory McpEndpoints.fromJson(Map<String, dynamic> json) {
    return McpEndpoints(
      id: json['id'] as int?,
      developerId: json['developer_id'] as int?,
      //map snake_case field
      name: json['name'] as String?,
      description: json['description'] as String?,
      enabled: json['enabled'] as int?,
      createdAt: json['created_at'] as String?,
      //map snake_case field
      updatedAt: json['updated_at'] as String?, //map snake_case field
    );
  }

  ///from JSON arrayconvertas McpEndpoints list
  static List<McpEndpoints> fromListJson(List<dynamic> jsonList) {
    //nullhandle + typeverify,avoidCrash
    if (jsonList.isEmpty) return [];

    return jsonList
        .where((item) => item is Map<String, dynamic>) //filter非 Map type元素
        .map((item) => McpEndpoints.fromJson(item as Map<String, dynamic>))
        .toList();
  }

  ///convertas JSON object(forNetworkrequest/存储)
  Map<String, dynamic> toJson() {
    return {
      'id': id,
      'developer_id': developerId, //转回 snake_case adaptafter端
      'name': name,
      'description': description,
      'enabled': enabled,
      'created_at': createdAt, //转回 snake_case adaptafter端
      'updated_at': updatedAt, //转回 snake_case adaptafter端
    };
  }
}