/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

class CommonMcpTool {
  String? endpointId;
  String? name;
  String? language;
  List<String> languages;

  // Creates a tool, deriving the language list from language if needed.
  CommonMcpTool({
    this.endpointId,
    this.name,
    this.language,
    List<String>? languages,
  }) : languages = languages ?? (language == null ? [] : [language]);

  // Creates a tool from a JSON map; language may be a string or a list.
  factory CommonMcpTool.fromJson(Map<String, dynamic> json) {
    final languages = _parseLanguages(json['language']);
    return CommonMcpTool(
      endpointId: json['endpoint_id'] as String?,
      name: json['name'] as String?,
      language: languages.isEmpty ? null : languages.first,
      languages: languages,
    );
  }

  // Converts this tool to a JSON map; a single language stays a string.
  Map<String, dynamic> toJson() {
    final Map<String, dynamic> data = <String, dynamic>{};
    data['endpoint_id'] = endpointId;
    data['name'] = name;
    if (languages.length > 1) {
      data['language'] = languages;
    } else {
      data['language'] = language;
    }
    return data;
  }

  // Converts a JSON array to a list of tools.
  static List<CommonMcpTool> fromListJson(List<dynamic> jsonList) {
    // Every array entry must be a JSON object.
    return jsonList
        .map(
          (jsonItem) =>
              CommonMcpTool.fromJson(jsonItem as Map<String, dynamic>),
        )
        .toList();
  }

  // Converts a list of tools to a JSON array.
  static List<Map<String, dynamic>> toListJson(List<CommonMcpTool>? toolList) {
    if (toolList == null) return [];
    return toolList.map((tool) => tool.toJson()).toList();
  }

  static List<String> _parseLanguages(dynamic value) {
    if (value == null) return [];
    if (value is List) {
      return value.map((item) => item.toString()).toList();
    }
    return [value.toString()];
  }
}
