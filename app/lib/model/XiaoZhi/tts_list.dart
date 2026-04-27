/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

class TTsList {
  //languagelist
  List<String> languages;

  //coreModify:anylanguage -> corresponding语音list,supportalllanguagetype
  Map<String, List<TTsVoice>>? ttsVoices;

  //Constructorfunction(NullsafedefaultValue)
  TTsList({this.languages = const [], this.ttsVoices});

  //JSON tomodel
  factory TTsList.fromJson(Map<String, dynamic> json) {
    //parselanguagelist
    final languageList = json['languages'] is List
        ? List<String>.from(json['languages'].map((x) => x.toString()))
        : <String>[];

    //parsedynamiclanguage语音 Map(core:autoadaptalllanguage key)
    Map<String, List<TTsVoice>>? voiceMap;
    if (json['tts_voices'] != null) {
      voiceMap = {};
      final jsonMap = json['tts_voices'] as Map;
      jsonMap.forEach((key, value) {
        if (value is List) {
          voiceMap![key.toString()] = value
              .map((item) => TTsVoice.fromJson(item as Map<String, dynamic>))
              .toList();
        }
      });
    }

    return TTsList(languages: languageList, ttsVoices: voiceMap);
  }

  //modelto JSON
  Map<String, dynamic> toJson() {
    final data = <String, dynamic>{};
    data['languages'] = languages;
    if (ttsVoices != null) {
      data['tts_voices'] = ttsVoices!.map(
        (key, value) =>
            MapEntry(key, value.map((item) => item.toJson()).toList()),
      );
    }
    return data;
  }
}

class TTsVoice {
  bool? top;
  String? voiceId;
  String? voiceName;
  String? language;
  String? createdAt;
  String? voiceDemo;

  TTsVoice({
    this.top,
    this.voiceId,
    this.voiceName,
    this.language,
    this.createdAt,
    this.voiceDemo,
  });

  //JSON tomodel
  factory TTsVoice.fromJson(Map<String, dynamic> json) {
    return TTsVoice(
      top: json['top'] as bool?,
      voiceId: json['voice_id']?.toString(),
      voiceName: json['voice_name']?.toString(),
      language: json['language']?.toString(),
      createdAt: json['created_at']?.toString(),
      voiceDemo: json['voice_demo']?.toString(),
    );
  }

  //modelto JSON
  Map<String, dynamic> toJson() {
    final data = <String, dynamic>{};
    data['top'] = top;
    data['voice_id'] = voiceId;
    data['voice_name'] = voiceName;
    data['language'] = language;
    data['created_at'] = createdAt;
    data['voice_demo'] = voiceDemo;
    return data;
  }
}
