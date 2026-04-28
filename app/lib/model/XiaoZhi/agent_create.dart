/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

class AgentCreate {
  String? agent_name;
  String? assistant_name;
  String? llm_model;
  String? tts_voice;
  String? tts_speech_speed;
  int? tts_pitch;
  String? asr_speed;
  String? language;
  String? character;
  String? memory;
  String? memory_type;
  List<dynamic>? mcp_endpoints;
  List<dynamic>? product_mcp_endpoints;

  //Constructorfunction
  AgentCreate({
    this.agent_name,
    this.assistant_name,
    this.llm_model,
    this.tts_voice,
    this.tts_speech_speed,
    this.tts_pitch,
    this.asr_speed,
    this.language,
    this.character,
    this.memory,
    this.memory_type,
    this.mcp_endpoints,
    this.product_mcp_endpoints,
  });

  //from JSON Deserialize(factory Factorymethod)
  factory AgentCreate.fromJson(Map<String, dynamic> json) {
    return AgentCreate(
      agent_name: json['agent_name'] as String?,
      assistant_name: json['assistant_name'] as String?,
      llm_model: json['llm_model'] as String?,
      tts_voice: json['tts_voice'] as String?,
      tts_speech_speed: json['tts_speech_speed'] as String?,
      tts_pitch: json['tts_pitch'] as int?,
      //Count / NumberValuetypeSeparatelyhandle
      asr_speed: json['asr_speed'] as String?,
      language: json['language'] as String?,
      character: json['character'] as String?,
      memory: json['memory'] as String?,
      memory_type: json['memory_type'] as String?,
      //handledynamicarray,directCast(List<dynamic> Compatible withanyarraytype)
      mcp_endpoints: json['mcp_endpoints'] as List<dynamic>?,
      product_mcp_endpoints: json['product_mcp_endpoints'] as List<dynamic>?,
    );
  }

  //Serializeas JSON(return Map<String, dynamic>),onlyContainsNot emptyfield
  Map<String, dynamic> toJson() {
    final Map<String, dynamic> data = <String, dynamic>{};
    //only whenfieldValueNotas null when,Only thenaddto Map in
    if (agent_name != null) data['agent_name'] = agent_name;
    if (assistant_name != null) data['assistant_name'] = assistant_name;
    if (llm_model != null) data['llm_model'] = llm_model;
    if (tts_voice != null) data['tts_voice'] = tts_voice;
    if (tts_speech_speed != null) data['tts_speech_speed'] = tts_speech_speed;
    if (tts_pitch != null) data['tts_pitch'] = tts_pitch;
    if (asr_speed != null) data['asr_speed'] = asr_speed;
    if (language != null) data['language'] = language;
    if (character != null) data['character'] = character;
    if (memory != null) data['memory'] = memory;
    if (memory_type != null) data['memory_type'] = memory_type;
    data['mcp_endpoints'] = mcp_endpoints;
    data['product_mcp_endpoints'] = product_mcp_endpoints;
    return data;
  }
}
