/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

class Conversation {
  int? id;
  int? userId;
  String? createdAt;
  int? deviceId;
  int? msgCount;
  int? agentId;
  String? model;
  int? tokenCount;
  int? duration;
  ChatSummary? chatSummary;

  // Creates a conversation record; every field is optional.
  Conversation({
    this.id,
    this.userId,
    this.createdAt,
    this.deviceId,
    this.msgCount,
    this.agentId,
    this.model,
    this.tokenCount,
    this.duration,
    this.chatSummary,
  });

  // Creates a conversation from a JSON map.
  factory Conversation.fromJson(Map<String, dynamic> json) {
    return Conversation(
      id: json['id'] as int?,
      userId: json['user_id'] as int?,
      createdAt: json['created_at'] as String?,
      deviceId: json['device_id'] as int?,
      msgCount: json['msg_count'] as int?,
      agentId: json['agent_id'] as int?,
      model: json['model'] as String?,
      tokenCount: json['token_count'] as int?,
      duration: json['duration'] as int?,
      // Parse the nested chat summary when present.
      chatSummary: json['chat_summary'] != null
          ? ChatSummary.fromJson(json['chat_summary'] as Map<String, dynamic>)
          : null,
    );
  }

  // Converts this conversation to a JSON map.
  Map<String, dynamic> toJson() {
    final Map<String, dynamic> data = <String, dynamic>{};
    data['id'] = id;
    data['user_id'] = userId;
    data['created_at'] = createdAt;
    data['device_id'] = deviceId;
    data['msg_count'] = msgCount;
    data['agent_id'] = agentId;
    data['model'] = model;
    data['token_count'] = tokenCount;
    data['duration'] = duration;
    // Only include the summary when it exists.
    if (chatSummary != null) {
      data['chat_summary'] = chatSummary!.toJson();
    }
    return data;
  }
}

class ChatSummary {
  String? title;
  String? summary;

  // Creates a chat summary.
  ChatSummary({this.title, this.summary});

  // Creates a chat summary from a JSON map.
  factory ChatSummary.fromJson(Map<String, dynamic> json) {
    return ChatSummary(
      title: json['title'] as String?,
      summary: json['summary'] as String?,
    );
  }

  // Converts this summary to a JSON map.
  Map<String, dynamic> toJson() {
    final Map<String, dynamic> data = <String, dynamic>{};
    data['title'] = title;
    data['summary'] = summary;
    return data;
  }
}
