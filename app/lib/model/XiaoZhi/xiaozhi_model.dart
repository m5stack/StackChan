/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

class XiaoZhiModel {
  List<ModelData>? modelList;

  //Constructorfunction
  XiaoZhiModel({this.modelList});

  //from JSON Deserialize
  factory XiaoZhiModel.fromJson(Map<String, dynamic> json) {
    //handlearraytype modelList
    var modelListJson = json['modelList'] as List?;
    List<ModelData>? modelList;
    if (modelListJson != null) {
      modelList = modelListJson
          .map((item) => ModelData.fromJson(item as Map<String, dynamic>))
          .toList();
    }

    return XiaoZhiModel(modelList: modelList);
  }

  //Serializeas JSON
  Map<String, dynamic> toJson() {
    final Map<String, dynamic> data = <String, dynamic>{};
    //handlearraytype modelList
    if (modelList != null) {
      data['modelList'] = modelList!.map((v) => v.toJson()).toList();
    }
    return data;
  }
}

class ModelData {
  String? name;
  String? description;
  List<String>? xiaozhi_version;
  List<String>? role;

  //Constructorfunction
  ModelData({this.name, this.description, this.xiaozhi_version, this.role});

  //from JSON Deserialize
  factory ModelData.fromJson(Map<String, dynamic> json) {
    return ModelData(
      name: json['name'] as String?,
      description: json['description'] as String?,
      //handleStringarray
      xiaozhi_version: json['xiaozhi_version'] != null
          ? List<String>.from(json['xiaozhi_version'] as List)
          : null,
      role: json['role'] != null
          ? List<String>.from(json['role'] as List)
          : null,
    );
  }

  //Serializeas JSON
  Map<String, dynamic> toJson() {
    final Map<String, dynamic> data = <String, dynamic>{};
    data['name'] = name;
    data['description'] = description;
    data['xiaozhi_version'] = xiaozhi_version;
    data['role'] = role;
    return data;
  }
}
