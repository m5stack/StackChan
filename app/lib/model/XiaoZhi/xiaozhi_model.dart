/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

class XiaoZhiModel {
  List<ModelData>? modelList;

  // Creates a model catalog; the list is optional.
  XiaoZhiModel({this.modelList});

  // Creates a model catalog from a JSON map.
  factory XiaoZhiModel.fromJson(Map<String, dynamic> json) {
    // Parse the modelList array.
    var modelListJson = json['modelList'] as List?;
    List<ModelData>? modelList;
    if (modelListJson != null) {
      modelList = modelListJson
          .map((item) => ModelData.fromJson(item as Map<String, dynamic>))
          .toList();
    }

    return XiaoZhiModel(modelList: modelList);
  }

  // Converts this catalog to a JSON map.
  Map<String, dynamic> toJson() {
    final Map<String, dynamic> data = <String, dynamic>{};
    // Only include the list when it is set.
    if (modelList != null) {
      data['modelList'] = modelList!.map((v) => v.toJson()).toList();
    }
    return data;
  }
}

class ModelData {
  String? name;
  String? description;
  List<String>? xiaozhiVersion;
  List<String>? role;

  // Creates a model entry; every field is optional.
  ModelData({this.name, this.description, this.xiaozhiVersion, this.role});

  // Creates a model entry from a JSON map.
  factory ModelData.fromJson(Map<String, dynamic> json) {
    return ModelData(
      name: json['name'] as String?,
      description: json['description'] as String?,
      // Parse the version string array.
      xiaozhiVersion: json['xiaozhi_version'] != null
          ? List<String>.from(json['xiaozhi_version'] as List)
          : null,
      role: json['role'] != null
          ? List<String>.from(json['role'] as List)
          : null,
    );
  }

  // Converts this model entry to a JSON map.
  Map<String, dynamic> toJson() {
    final Map<String, dynamic> data = <String, dynamic>{};
    data['name'] = name;
    data['description'] = description;
    data['xiaozhi_version'] = xiaozhiVersion;
    data['role'] = role;
    return data;
  }
}
