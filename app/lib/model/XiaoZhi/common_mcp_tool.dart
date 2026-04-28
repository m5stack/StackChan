/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

class CommonMcpTool {
  String? endpoint_id;
  String? name;
  String? language;

  //Constructorfunction
  CommonMcpTool({this.endpoint_id, this.name, this.language});

  //1. singleobjectfrom JSON Deserialize
  factory CommonMcpTool.fromJson(Map<String, dynamic> json) {
    return CommonMcpTool(
      endpoint_id: json['endpoint_id'] as String?,
      name: json['name'] as String?,
      language: json['language'] as String?,
    );
  }

  //2. singleobjectSerializeas JSON
  Map<String, dynamic> toJson() {
    final Map<String, dynamic> data = <String, dynamic>{};
    data['endpoint_id'] = endpoint_id;
    data['name'] = name;
    data['language'] = language;
    return data;
  }

  //3. JSON arrayconvertas CommonMcpTool list(core FromListJson Implement)
  static List<CommonMcpTool> fromListJson(List<dynamic> jsonList) {
    //iterate JSON array，one by oneconvertas CommonMcpTool object
    return jsonList
        .map(
          (jsonItem) =>
              CommonMcpTool.fromJson(jsonItem as Map<String, dynamic>),
        )
        .toList();
  }

  //optionalExtension:CommonMcpTool listconvertas JSON array
  static List<Map<String, dynamic>> toListJson(List<CommonMcpTool>? toolList) {
    if (toolList == null) return [];
    return toolList.map((tool) => tool.toJson()).toList();
  }
}
