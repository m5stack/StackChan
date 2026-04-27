/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

class Conversation {
  int? id;
  int? user_id;
  String? created_at;
  int? device_id;
  int? msg_count;
  int? agent_id;
  String? model;
  int? token_count;
  int? duration;
  ChatSummary? chat_summary;

  //Constructorfunction
  Conversation({
    this.id,
    this.user_id,
    this.created_at,
    this.device_id,
    this.msg_count,
    this.agent_id,
    this.model,
    this.token_count,
    this.duration,
    this.chat_summary,
  });

  //fromJSONconvert
  factory Conversation.fromJson(Map<String, dynamic> json) {
    return Conversation(
      id: json['id'] as int?,
      user_id: json['user_id'] as int?,
      created_at: json['created_at'] as String?,
      device_id: json['device_id'] as int?,
      msg_count: json['msg_count'] as int?,
      agent_id: json['agent_id'] as int?,
      model: json['model'] as String?,
      token_count: json['token_count'] as int?,
      duration: json['duration'] as int?,
      //nestedobjectconvert
      chat_summary: json['chat_summary'] != null
          ? ChatSummary.fromJson(json['chat_summary'] as Map<String, dynamic>)
          : null,
    );
  }

  //convertasJSON
  Map<String, dynamic> toJson() {
    final Map<String, dynamic> data = <String, dynamic>{};
    data['id'] = id;
    data['user_id'] = user_id;
    data['created_at'] = created_at;
    data['device_id'] = device_id;
    data['msg_count'] = msg_count;
    data['agent_id'] = agent_id;
    data['model'] = model;
    data['token_count'] = token_count;
    data['duration'] = duration;
    //handlenestedobjectJSONconvert
    if (chat_summary != null) {
      data['chat_summary'] = chat_summary!.toJson();
    }
    return data;
  }
}

class ChatSummary {
  String? title;
  String? summary;

  //Constructorfunction
  ChatSummary({this.title, this.summary});

  //fromJSONconvert
  factory ChatSummary.fromJson(Map<String, dynamic> json) {
    return ChatSummary(
      title: json['title'] as String?,
      summary: json['summary'] as String?,
    );
  }

  //convertasJSON
  Map<String, dynamic> toJson() {
    final Map<String, dynamic> data = <String, dynamic>{};
    data['title'] = title;
    data['summary'] = summary;
    return data;
  }
}
