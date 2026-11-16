/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

class AgentTemplate {
  static List<String>? _stringList(dynamic value) {
    if (value is! List) return null;
    return value.map((item) => item.toString()).toList();
  }

  static List<int>? _intList(dynamic value) {
    if (value is! List) return null;
    return value
        .map((item) => item is int ? item : int.tryParse(item.toString()))
        .whereType<int>()
        .toList();
  }

  int? id;
  int? developerId;
  String? agentName;
  List<String>? ttsVoices;
  String? defaultTtsVoice;
  String? llmModel;
  String? assistantName;
  String? userName;
  String? createdAt;
  String? updatedAt;
  String? character;
  String? ttsSpeechSpeed;
  String? asrSpeed;
  int? ttsPitch;
  List<int>? knowledgeBaseIds;
  String? xiaozhiVersion;
  String? ttsVoiceName;

  // Creates a template; every field is optional.
  AgentTemplate({
    this.id,
    this.developerId,
    this.agentName,
    this.ttsVoices,
    this.defaultTtsVoice,
    this.llmModel,
    this.assistantName,
    this.userName,
    this.createdAt,
    this.updatedAt,
    this.character,
    this.ttsSpeechSpeed,
    this.asrSpeed,
    this.ttsPitch,
    this.knowledgeBaseIds,
    this.xiaozhiVersion,
    this.ttsVoiceName,
  });

  // Creates a template from a JSON map.
  factory AgentTemplate.fromJson(Map<String, dynamic> json) {
    return AgentTemplate(
      id: json['id'] as int?,
      developerId: json['developer_id'] as int?,
      agentName: json['agent_name'] as String?,
      // List fields are normalized; malformed values become null.
      ttsVoices: _stringList(json['tts_voices']),
      defaultTtsVoice: json['default_tts_voice'] as String?,
      llmModel: json['llm_model'] as String?,
      assistantName: json['assistant_name'] as String?,
      userName: json['user_name'] as String?,
      createdAt: json['created_at'] as String?,
      updatedAt: json['updated_at'] as String?,
      character: json['character'] as String?,
      ttsSpeechSpeed: json['tts_speech_speed'] as String?,
      asrSpeed: json['asr_speed'] as String?,
      ttsPitch: json['tts_pitch'] as int?,
      knowledgeBaseIds: _intList(json['knowledge_base_ids']),
      // Remaining fields are optional strings.
      xiaozhiVersion: json['xiaozhi_version'] as String?,
      ttsVoiceName: json['tts_voice_name'] as String?,
    );
  }

  // Converts this template to a JSON map, omitting unset fields.
  Map<String, dynamic> toJson() {
    final Map<String, dynamic> data = <String, dynamic>{};
    if (id != null) data['id'] = id;
    if (developerId != null) data['developer_id'] = developerId;
    if (agentName != null) data['agent_name'] = agentName;
    if (ttsVoices != null) data['tts_voices'] = ttsVoices;
    if (defaultTtsVoice != null) data['default_tts_voice'] = defaultTtsVoice;
    if (llmModel != null) data['llm_model'] = llmModel;
    if (assistantName != null) data['assistant_name'] = assistantName;
    if (userName != null) data['user_name'] = userName;
    if (createdAt != null) data['created_at'] = createdAt;
    if (updatedAt != null) data['updated_at'] = updatedAt;
    if (character != null) data['character'] = character;
    if (ttsSpeechSpeed != null) data['tts_speech_speed'] = ttsSpeechSpeed;
    if (asrSpeed != null) data['asr_speed'] = asrSpeed;
    if (ttsPitch != null) data['tts_pitch'] = ttsPitch;
    if (knowledgeBaseIds != null) data['knowledge_base_ids'] = knowledgeBaseIds;
    if (xiaozhiVersion != null) data['xiaozhi_version'] = xiaozhiVersion;
    if (ttsVoiceName != null) data['tts_voice_name'] = ttsVoiceName;
    return data;
  }
}
