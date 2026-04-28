/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

class ConversationMessageData {
  int? id;
  int? user_id;
  int? chat_id;
  String? role;
  String? content;
  String? voice_embedding_id;
  String? created_at;
  String? name;
  int? prompt_tokens;
  int? total_tokens;
  int? completion_tokens;
  int? prompt_ms;
  int? total_ms;
  int? completion_ms;
  String? model;
  String? url;

  //optionalparameterConstructorfunction
  ConversationMessageData({
    this.id,
    this.user_id,
    this.chat_id,
    this.role,
    this.content,
    this.voice_embedding_id,
    this.created_at,
    this.name,
    this.prompt_tokens,
    this.total_tokens,
    this.completion_tokens,
    this.prompt_ms,
    this.total_ms,
    this.completion_ms,
    this.model,
    this.url,
  });

  //fromJSONconvert(Factorymethod,handlenullandtypesafe)
  factory ConversationMessageData.fromJson(Map<String, dynamic> json) {
    return ConversationMessageData(
      id: json['id'] is int ? json['id'] as int : null,
      user_id: json['user_id'] is int ? json['user_id'] as int : null,
      chat_id: json['chat_id'] is int ? json['chat_id'] as int : null,
      role: json['role'] is String ? json['role'] as String : null,
      content: json['content'] is String ? json['content'] as String : null,
      voice_embedding_id: json['voice_embedding_id'] is String
          ? json['voice_embedding_id'] as String
          : null,
      created_at: json['created_at'] is String
          ? json['created_at'] as String
          : null,
      name: json['name'] is String ? json['name'] as String : null,
      prompt_tokens: json['prompt_tokens'] is int
          ? json['prompt_tokens'] as int
          : null,
      total_tokens: json['total_tokens'] is int
          ? json['total_tokens'] as int
          : null,
      completion_tokens: json['completion_tokens'] is int
          ? json['completion_tokens'] as int
          : null,
      prompt_ms: json['prompt_ms'] is int ? json['prompt_ms'] as int : null,
      total_ms: json['total_ms'] is int ? json['total_ms'] as int : null,
      completion_ms: json['completion_ms'] is int
          ? json['completion_ms'] as int
          : null,
      model: json['model'] is String ? json['model'] as String : null,
      url: json['url'] is String ? json['url'] as String : null,
    );
  }

  //convertasJSON(handlenull,avoidnullfieldPollute / Add unnecessaryJSON)
  Map<String, dynamic> toJson() {
    final Map<String, dynamic> data = <String, dynamic>{};

    //onlyaddNot emptyfield,ReduceJSONSize
    if (id != null) data['id'] = id;
    if (user_id != null) data['user_id'] = user_id;
    if (chat_id != null) data['chat_id'] = chat_id;
    if (role != null) data['role'] = role;
    if (content != null) data['content'] = content;
    if (voice_embedding_id != null) {
      data['voice_embedding_id'] = voice_embedding_id;
    }
    if (created_at != null) data['created_at'] = created_at;
    if (name != null) data['name'] = name;
    if (prompt_tokens != null) data['prompt_tokens'] = prompt_tokens;
    if (total_tokens != null) data['total_tokens'] = total_tokens;
    if (completion_tokens != null) {
      data['completion_tokens'] = completion_tokens;
    }
    if (prompt_ms != null) data['prompt_ms'] = prompt_ms;
    if (total_ms != null) data['total_ms'] = total_ms;
    if (completion_ms != null) data['completion_ms'] = completion_ms;
    if (model != null) data['model'] = model;
    if (url != null) data['url'] = url;
    return data;
  }
}
