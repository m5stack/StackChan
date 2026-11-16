/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

class McpEndpoints {
  int? id;
  int? developerId; // camelCase in Dart, snake_case in the API
  String? name;
  String? description;
  int? enabled;
  String? createdAt; // camelCase in Dart, snake_case in the API
  String? updatedAt; // camelCase in Dart, snake_case in the API

  // Creates an endpoint record with every field optional.
  McpEndpoints({
    this.id,
    this.developerId,
    this.name,
    this.description,
    this.enabled,
    this.createdAt,
    this.updatedAt,
  });

  /// Creates an endpoint record from a JSON object.
  factory McpEndpoints.fromJson(Map<String, dynamic> json) {
    return McpEndpoints(
      id: json['id'] as int?,
      developerId: json['developer_id'] as int?,
      // snake_case from the API
      name: json['name'] as String?,
      description: json['description'] as String?,
      enabled: json['enabled'] as int?,
      createdAt: json['created_at'] as String?,
      // snake_case from the API
      updatedAt: json['updated_at'] as String?, // snake_case from the API
    );
  }

  /// Creates endpoint records from a JSON array.
  static List<McpEndpoints> fromListJson(List<dynamic> jsonList) {
    // Guard against empty input and non-object entries.
    if (jsonList.isEmpty) return [];

    return jsonList
        .whereType<Map<String, dynamic>>() // Ignore entries that are not JSON objects
        .map(McpEndpoints.fromJson)
        .toList();
  }

  /// Converts the record to JSON for network requests or storage.
  Map<String, dynamic> toJson() {
    return {
      'id': id,
      'developer_id': developerId, // snake_case expected by the API
      'name': name,
      'description': description,
      'enabled': enabled,
      'created_at': createdAt, // snake_case expected by the API
      'updated_at': updatedAt, // snake_case expected by the API
    };
  }
}
