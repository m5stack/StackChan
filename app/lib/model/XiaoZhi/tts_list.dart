/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

class TTsList {
  // Languages supported by the TTS service.
  List<String> languages;

  // Voices grouped by language key; unknown keys are kept as-is.
  Map<String, List<TTsVoice>>? ttsVoices;

  // Creates a TTS catalog; languages default to an empty list.
  TTsList({this.languages = const [], this.ttsVoices});

  // Creates a TTS catalog from a JSON map.
  factory TTsList.fromJson(Map<String, dynamic> json) {
    // Parse the language list.
    final languageList = json['languages'] is List
        ? List<String>.from(json['languages'].map((x) => x.toString()))
        : <String>[];

    // Parse the dynamic map of voices keyed by language.
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

  // Converts this catalog to a JSON map.
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

  // Creates a voice from a JSON map.
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

  // Converts this voice to a JSON map.
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
