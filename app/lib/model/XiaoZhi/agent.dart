/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

import '../XiaoZhi/device.dart';

class Agent {
  int? id;
  int? user_id;
  String? agent_name;
  String? tts_voice;
  String? llm_model;
  String? assistant_name;
  String? user_name;
  String? created_at;
  String? updated_at;
  String? memory;
  String? character;
  int? long_memory_switch;
  String? lang_code;
  String? language;
  String? tts_speech_speed;
  String? asr_speed;
  int? is_deleted;
  int? tts_pitch;
  int? agent_template_id;
  List<int>? knowledge_base_ids;
  String? memory_updated_at;
  int? share_agent_id;
  String? source;
  List<String>? mcp_endpoints;
  String? memory_type;
  int? max_message_count;
  int? deviceCount;
  LastDevice? lastDevice;

  //Constructorfunction
  Agent({
    this.id,
    this.user_id,
    this.agent_name,
    this.tts_voice,
    this.llm_model,
    this.assistant_name,
    this.user_name,
    this.created_at,
    this.updated_at,
    this.memory,
    this.character,
    this.long_memory_switch,
    this.lang_code,
    this.language,
    this.tts_speech_speed,
    this.asr_speed,
    this.is_deleted,
    this.tts_pitch,
    this.agent_template_id,
    this.knowledge_base_ids,
    this.memory_updated_at,
    this.share_agent_id,
    this.source,
    this.mcp_endpoints,
    this.memory_type,
    this.max_message_count,
    this.deviceCount,
    this.lastDevice,
  });

  //fromsingle JSON objectDeserialize
  factory Agent.fromJson(Map<String, dynamic> json) {
    return Agent(
      id: json['id'] as int?,
      user_id: json['user_id'] as int?,
      agent_name: json['agent_name'] as String?,
      tts_voice: json['tts_voice'] as String?,
      llm_model: json['llm_model'] as String?,
      assistant_name: json['assistant_name'] as String?,
      user_name: json['user_name'] as String?,
      created_at: json['created_at'] as String?,
      updated_at: json['updated_at'] as String?,
      memory: json['memory'] as String?,
      character: json['character'] as String?,
      long_memory_switch: json['long_memory_switch'] as int?,
      lang_code: json['lang_code'] as String?,
      language: json['language'] as String?,
      tts_speech_speed: json['tts_speech_speed'] as String?,
      asr_speed: json['asr_speed'] as String?,
      is_deleted: json['is_deleted'] as int?,
      tts_pitch: json['tts_pitch'] as int?,
      agent_template_id: json['agent_template_id'] as int?,
      knowledge_base_ids: json['knowledge_base_ids'] != null
          ? List<int>.from(json['knowledge_base_ids'] as List)
          : null,
      memory_updated_at: json['memory_updated_at'] as String?,
      share_agent_id: json['share_agent_id'] as int?,
      source: json['source'] as String?,
      mcp_endpoints: json['mcp_endpoints'] != null
          ? List<String>.from(json['mcp_endpoints'] as List)
          : null,
      memory_type: json['memory_type'] as String?,
      max_message_count: json['max_message_count'] as int?,
      deviceCount: json['deviceCount'] as int?,
      lastDevice: json['lastDevice'] != null && json['lastDevice'] is Map
          ? LastDevice.fromJson(json['lastDevice'] as Map<String, dynamic>)
          : null,
    );
  }

  //newIncrease / Add:from JSON arrayconvertas List<Agent>
  static List<Agent> fromListJson(List<dynamic>? jsonList) {
    //nullFallback:ifPassed null/Nullarray,returnNulllist
    if (jsonList == null || jsonList.isEmpty) {
      return [];
    }
    //iteratearray，one by oneconvertas Agent object
    return jsonList.map((jsonItem) {
      //Fallback / Error handlinghandle:EnsureeachElementis Map type
      if (jsonItem is Map<String, dynamic>) {
        return Agent.fromJson(jsonItem);
      } else {
        //Non- Map typereturnNull Agent(oraccording to需求抛Throws/skip)
        return Agent();
      }
    }).toList();
  }

  //Serializeassingle JSON object
  Map<String, dynamic> toJson() {
    final Map<String, dynamic> data = <String, dynamic>{};
    data['id'] = id;
    data['user_id'] = user_id;
    data['agent_name'] = agent_name;
    data['tts_voice'] = tts_voice;
    data['llm_model'] = llm_model;
    data['assistant_name'] = assistant_name;
    data['user_name'] = user_name;
    data['created_at'] = created_at;
    data['updated_at'] = updated_at;
    data['memory'] = memory;
    data['character'] = character;
    data['long_memory_switch'] = long_memory_switch;
    data['lang_code'] = lang_code;
    data['language'] = language;
    data['tts_speech_speed'] = tts_speech_speed;
    data['asr_speed'] = asr_speed;
    data['is_deleted'] = is_deleted;
    data['tts_pitch'] = tts_pitch;
    data['agent_template_id'] = agent_template_id;
    data['knowledge_base_ids'] = knowledge_base_ids;
    data['memory_updated_at'] = memory_updated_at;
    data['share_agent_id'] = share_agent_id;
    data['source'] = source;
    data['mcp_endpoints'] = mcp_endpoints;
    data['memory_type'] = memory_type;
    data['max_message_count'] = max_message_count;
    data['deviceCount'] = deviceCount;
    if (lastDevice != null) {
      data['lastDevice'] = lastDevice!.toJson();
    }
    return data;
  }

  //optionalExtension:will List<Agent> convertas JSON array
  static List<Map<String, dynamic>> toListJson(List<Agent>? agentList) {
    if (agentList == null || agentList.isEmpty) {
      return [];
    }
    return agentList.map((agent) => agent.toJson()).toList();
  }
}
