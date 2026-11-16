/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

class AgentCreate {
  String? agentName;
  String? llmModel;
  String? ttsVoice;
  String? ttsSpeechSpeed;
  int? ttsPitch;
  String? asrSpeed;
  String? language;
  String? character;
  String? memory;
  String? memoryType;
  List<dynamic>? mcpEndpoints;
  List<dynamic>? productMcpEndpoints;

  // Creates a request payload for creating an agent.
  AgentCreate({
    this.agentName,
    this.llmModel,
    this.ttsVoice,
    this.ttsSpeechSpeed,
    this.ttsPitch,
    this.asrSpeed,
    this.language,
    this.character,
    this.memory,
    this.memoryType,
    this.mcpEndpoints,
    this.productMcpEndpoints,
  });

  // Creates a payload from a JSON map.
  factory AgentCreate.fromJson(Map<String, dynamic> json) {
    return AgentCreate(
      agentName: json['agent_name'] as String?,
      llmModel: json['llm_model'] as String?,
      ttsVoice: json['tts_voice'] as String?,
      ttsSpeechSpeed: json['tts_speech_speed'] as String?,
      ttsPitch: json['tts_pitch'] as int?,
      // The API returns the speech rate as a string.
      asrSpeed: json['asr_speed'] as String?,
      language: json['language'] as String?,
      character: json['character'] as String?,
      memory: json['memory'] as String?,
      memoryType: json['memory_type'] as String?,
      // Endpoint lists stay as raw JSON arrays.
      mcpEndpoints: json['mcp_endpoints'] as List<dynamic>?,
      productMcpEndpoints: json['product_mcp_endpoints'] as List<dynamic>?,
    );
  }

  // Converts this payload to a JSON map, omitting unset fields.
  Map<String, dynamic> toJson() {
    final Map<String, dynamic> data = <String, dynamic>{};
    // Only include fields that were set.
    if (agentName != null) data['agent_name'] = agentName;
    if (llmModel != null) data['llm_model'] = llmModel;
    if (ttsVoice != null) data['tts_voice'] = ttsVoice;
    if (ttsSpeechSpeed != null) data['tts_speech_speed'] = ttsSpeechSpeed;
    if (ttsPitch != null) data['tts_pitch'] = ttsPitch;
    if (asrSpeed != null) data['asr_speed'] = asrSpeed;
    if (language != null) data['language'] = language;
    if (character != null) data['character'] = character;
    if (memory != null) data['memory'] = memory;
    if (memoryType != null) data['memory_type'] = memoryType;
    data['mcp_endpoints'] = mcpEndpoints;
    data['product_mcp_endpoints'] = productMcpEndpoints;
    return data;
  }
}
