/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

class AgentTemplate {
  int? id;
  int? developer_id;
  String? agent_name;
  List<String>? tts_voices;
  String? default_tts_voice;
  String? llm_model;
  String? assistant_name;
  String? user_name;
  String? created_at;
  String? updated_at;
  String? character;
  String? tts_speech_speed;
  String? asr_speed;
  int? tts_pitch;
  List<int>? knowledge_base_ids;
  String? xiaozhi_version;
  String? tts_voice_name;

  //defaultConstructorfunction
  AgentTemplate({
    this.id,
    this.developer_id,
    this.agent_name,
    this.tts_voices,
    this.default_tts_voice,
    this.llm_model,
    this.assistant_name,
    this.user_name,
    this.created_at,
    this.updated_at,
    this.character,
    this.tts_speech_speed,
    this.asr_speed,
    this.tts_pitch,
    this.knowledge_base_ids,
    this.xiaozhi_version,
    this.tts_voice_name,
  });

  //fromJSONparseFactorymethod
  factory AgentTemplate.fromJson(Map<String, dynamic> json) {
    return AgentTemplate(
      id: json['id'] as int?,
      developer_id: json['developer_id'] as int?,
      agent_name: json['agent_name'] as String?,
      //handlelisttype,avoidnullorNon-listtypeCauseCrash
      tts_voices: json['tts_voices'] != null
          ? List<String>.from(json['tts_voices'] as List)
          : null,
      default_tts_voice: json['default_tts_voice'] as String?,
      llm_model: json['llm_model'] as String?,
      assistant_name: json['assistant_name'] as String?,
      user_name: json['user_name'] as String?,
      created_at: json['created_at'] as String?,
      updated_at: json['updated_at'] as String?,
      character: json['character'] as String?,
      tts_speech_speed: json['tts_speech_speed'] as String?,
      asr_speed: json['asr_speed'] as String?,
      tts_pitch: json['tts_pitch'] as int?,
      knowledge_base_ids: json['knowledge_base_ids'],
      //dynamictypedirectAssignValue
      xiaozhi_version: json['xiaozhi_version'] as String?,
      tts_voice_name: json['tts_voice_name'] as String?,
    );
  }

  //convertasJSONmethod
  Map<String, dynamic> toJson() {
    final Map<String, dynamic> data = <String, dynamic>{};
    if (id != null) data['id'] = id;
    if (developer_id != null) data['developer_id'] = developer_id;
    if (agent_name != null) data['agent_name'] = agent_name;
    if (tts_voices != null) data['tts_voices'] = tts_voices;
    if (default_tts_voice != null)
      data['default_tts_voice'] = default_tts_voice;
    if (llm_model != null) data['llm_model'] = llm_model;
    if (assistant_name != null) data['assistant_name'] = assistant_name;
    if (user_name != null) data['user_name'] = user_name;
    if (created_at != null) data['created_at'] = created_at;
    if (updated_at != null) data['updated_at'] = updated_at;
    if (character != null) data['character'] = character;
    if (tts_speech_speed != null) data['tts_speech_speed'] = tts_speech_speed;
    if (asr_speed != null) data['asr_speed'] = asr_speed;
    if (tts_pitch != null) data['tts_pitch'] = tts_pitch;
    if (knowledge_base_ids != null)
      data['knowledge_base_ids'] = knowledge_base_ids;
    if (xiaozhi_version != null) data['xiaozhi_version'] = xiaozhi_version;
    if (tts_voice_name != null) data['tts_voice_name'] = tts_voice_name;
    return data;
  }
}
