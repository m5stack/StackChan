/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

import '../XiaoZhi/device.dart';

class Agent {
  int? id;
  int? userId;
  String? agentName;
  String? ttsVoice;
  String? llmModel;
  String? assistantName;
  String? userName;
  String? createdAt;
  String? updatedAt;
  String? memory;
  String? character;
  int? longMemorySwitch;
  String? langCode;
  String? language;
  String? ttsSpeechSpeed;
  String? asrSpeed;
  int? isDeleted;
  int? ttsPitch;
  int? agentTemplateId;
  List<int>? knowledgeBaseIds;
  String? memoryUpdatedAt;
  int? shareAgentId;
  String? source;
  List<String>? mcpEndpoints;
  String? memoryType;
  int? maxMessageCount;
  int? deviceCount;
  LastDevice? lastDevice;

  // Creates an agent; every field is optional.
  Agent({
    this.id,
    this.userId,
    this.agentName,
    this.ttsVoice,
    this.llmModel,
    this.assistantName,
    this.userName,
    this.createdAt,
    this.updatedAt,
    this.memory,
    this.character,
    this.longMemorySwitch,
    this.langCode,
    this.language,
    this.ttsSpeechSpeed,
    this.asrSpeed,
    this.isDeleted,
    this.ttsPitch,
    this.agentTemplateId,
    this.knowledgeBaseIds,
    this.memoryUpdatedAt,
    this.shareAgentId,
    this.source,
    this.mcpEndpoints,
    this.memoryType,
    this.maxMessageCount,
    this.deviceCount,
    this.lastDevice,
  });

  // Creates an agent from a JSON map.
  factory Agent.fromJson(Map<String, dynamic> json) {
    return Agent(
      id: json['id'] as int?,
      userId: json['user_id'] as int?,
      agentName: json['agent_name'] as String?,
      ttsVoice: json['tts_voice'] as String?,
      llmModel: json['llm_model'] as String?,
      assistantName: json['assistant_name'] as String?,
      userName: json['user_name'] as String?,
      createdAt: json['created_at'] as String?,
      updatedAt: json['updated_at'] as String?,
      memory: json['memory'] as String?,
      character: json['character'] as String?,
      longMemorySwitch: json['long_memory_switch'] as int?,
      langCode: json['lang_code'] as String?,
      language: json['language'] as String?,
      ttsSpeechSpeed: json['tts_speech_speed'] as String?,
      asrSpeed: json['asr_speed'] as String?,
      isDeleted: json['is_deleted'] as int?,
      ttsPitch: json['tts_pitch'] as int?,
      agentTemplateId: json['agent_template_id'] as int?,
      knowledgeBaseIds: json['knowledge_base_ids'] != null
          ? List<int>.from(json['knowledge_base_ids'] as List)
          : null,
      memoryUpdatedAt: json['memory_updated_at'] as String?,
      shareAgentId: json['share_agent_id'] as int?,
      source: json['source'] as String?,
      mcpEndpoints: json['mcp_endpoints'] != null
          ? List<String>.from(json['mcp_endpoints'] as List)
          : null,
      memoryType: json['memory_type'] as String?,
      maxMessageCount: json['max_message_count'] as int?,
      deviceCount: json['deviceCount'] as int?,
      lastDevice: json['lastDevice'] != null && json['lastDevice'] is Map
          ? LastDevice.fromJson(json['lastDevice'] as Map<String, dynamic>)
          : null,
    );
  }

  // Parses a JSON array into a list of agents.
  static List<Agent> fromListJson(List<dynamic>? jsonList) {
    // Return an empty list for null or empty input.
    if (jsonList == null || jsonList.isEmpty) {
      return [];
    }
    // Map each array entry to an agent.
    return jsonList.map((jsonItem) {
      // Parse entries that are JSON objects.
      if (jsonItem is Map<String, dynamic>) {
        return Agent.fromJson(jsonItem);
      } else {
        // Fall back to an empty agent for malformed entries.
        return Agent();
      }
    }).toList();
  }

  // Converts this agent to a JSON map.
  Map<String, dynamic> toJson() {
    final Map<String, dynamic> data = <String, dynamic>{};
    data['id'] = id;
    data['user_id'] = userId;
    data['agent_name'] = agentName;
    data['tts_voice'] = ttsVoice;
    data['llm_model'] = llmModel;
    data['assistant_name'] = assistantName;
    data['user_name'] = userName;
    data['created_at'] = createdAt;
    data['updated_at'] = updatedAt;
    data['memory'] = memory;
    data['character'] = character;
    data['long_memory_switch'] = longMemorySwitch;
    data['lang_code'] = langCode;
    data['language'] = language;
    data['tts_speech_speed'] = ttsSpeechSpeed;
    data['asr_speed'] = asrSpeed;
    data['is_deleted'] = isDeleted;
    data['tts_pitch'] = ttsPitch;
    data['agent_template_id'] = agentTemplateId;
    data['knowledge_base_ids'] = knowledgeBaseIds;
    data['memory_updated_at'] = memoryUpdatedAt;
    data['share_agent_id'] = shareAgentId;
    data['source'] = source;
    data['mcp_endpoints'] = mcpEndpoints;
    data['memory_type'] = memoryType;
    data['max_message_count'] = maxMessageCount;
    data['deviceCount'] = deviceCount;
    if (lastDevice != null) {
      data['lastDevice'] = lastDevice!.toJson();
    }
    return data;
  }

  // Converts a list of agents to a JSON array.
  static List<Map<String, dynamic>> toListJson(List<Agent>? agentList) {
    if (agentList == null || agentList.isEmpty) {
      return [];
    }
    return agentList.map((agent) => agent.toJson()).toList();
  }
}
