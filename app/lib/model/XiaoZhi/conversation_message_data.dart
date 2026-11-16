/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

class ConversationMessageData {
  int? id;
  int? userId;
  int? chatId;
  String? role;
  String? content;
  String? voiceEmbeddingId;
  String? createdAt;
  String? name;
  int? promptTokens;
  int? totalTokens;
  int? completionTokens;
  int? promptMs;
  int? totalMs;
  int? completionMs;
  String? model;
  String? url;

  // Creates a message record; every field is optional.
  ConversationMessageData({
    this.id,
    this.userId,
    this.chatId,
    this.role,
    this.content,
    this.voiceEmbeddingId,
    this.createdAt,
    this.name,
    this.promptTokens,
    this.totalTokens,
    this.completionTokens,
    this.promptMs,
    this.totalMs,
    this.completionMs,
    this.model,
    this.url,
  });

  // Creates a message from a JSON map, ignoring values of unexpected type.
  factory ConversationMessageData.fromJson(Map<String, dynamic> json) {
    return ConversationMessageData(
      id: json['id'] is int ? json['id'] as int : null,
      userId: json['user_id'] is int ? json['user_id'] as int : null,
      chatId: json['chat_id'] is int ? json['chat_id'] as int : null,
      role: json['role'] is String ? json['role'] as String : null,
      content: json['content'] is String ? json['content'] as String : null,
      voiceEmbeddingId: json['voice_embedding_id'] is String
          ? json['voice_embedding_id'] as String
          : null,
      createdAt: json['created_at'] is String
          ? json['created_at'] as String
          : null,
      name: json['name'] is String ? json['name'] as String : null,
      promptTokens: json['prompt_tokens'] is int
          ? json['prompt_tokens'] as int
          : null,
      totalTokens: json['total_tokens'] is int
          ? json['total_tokens'] as int
          : null,
      completionTokens: json['completion_tokens'] is int
          ? json['completion_tokens'] as int
          : null,
      promptMs: json['prompt_ms'] is int ? json['prompt_ms'] as int : null,
      totalMs: json['total_ms'] is int ? json['total_ms'] as int : null,
      completionMs: json['completion_ms'] is int
          ? json['completion_ms'] as int
          : null,
      model: json['model'] is String ? json['model'] as String : null,
      url: json['url'] is String ? json['url'] as String : null,
    );
  }

  // Converts this message to a JSON map, omitting unset fields.
  Map<String, dynamic> toJson() {
    final Map<String, dynamic> data = <String, dynamic>{};

    // Skip unset fields to keep the payload small.
    if (id != null) data['id'] = id;
    if (userId != null) data['user_id'] = userId;
    if (chatId != null) data['chat_id'] = chatId;
    if (role != null) data['role'] = role;
    if (content != null) data['content'] = content;
    if (voiceEmbeddingId != null) {
      data['voice_embedding_id'] = voiceEmbeddingId;
    }
    if (createdAt != null) data['created_at'] = createdAt;
    if (name != null) data['name'] = name;
    if (promptTokens != null) data['prompt_tokens'] = promptTokens;
    if (totalTokens != null) data['total_tokens'] = totalTokens;
    if (completionTokens != null) {
      data['completion_tokens'] = completionTokens;
    }
    if (promptMs != null) data['prompt_ms'] = promptMs;
    if (totalMs != null) data['total_ms'] = totalMs;
    if (completionMs != null) data['completion_ms'] = completionMs;
    if (model != null) data['model'] = model;
    if (url != null) data['url'] = url;
    return data;
  }
}
