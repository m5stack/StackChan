/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

package service

import (
	"testing"

	xiaozhiModel "stackChan/internal/model/xiaozhi"
)

func TestGetTemplateTtsVoicePrefersDefaultVoice(t *testing.T) {
	template := xiaozhiModel.AgentTemplate{
		DefaultTtsVoice: "zh:Cherry",
		TtsVoices:       []string{"en:Alloy"},
	}

	if got := getTemplateTtsVoice(template); got != "Cherry" {
		t.Fatalf("getTemplateTtsVoice() = %q, want %q", got, "Cherry")
	}
}

func TestGetTemplateTtsVoiceUsesVoiceLanguage(t *testing.T) {
	template := xiaozhiModel.AgentTemplate{DefaultTtsVoice: "zh:Cherry"}

	voice, language := getTemplateTtsVoiceAndLanguage(template)
	if voice != "Cherry" || language != "zh" {
		t.Fatalf("getTemplateTtsVoiceAndLanguage() = (%q, %q), want (%q, %q)", voice, language, "Cherry", "zh")
	}
}

func TestGetTemplateTtsVoiceFallsBackToAvailableVoice(t *testing.T) {
	template := xiaozhiModel.AgentTemplate{
		TtsVoices: []string{"en:Alloy", "zh:Cherry"},
	}

	if got := getTemplateTtsVoice(template); got != "Alloy" {
		t.Fatalf("getTemplateTtsVoice() = %q, want %q", got, "Alloy")
	}
}

func TestGetTemplateTtsVoiceAcceptsUnprefixedDefaultVoice(t *testing.T) {
	template := xiaozhiModel.AgentTemplate{DefaultTtsVoice: "  Alloy  "}

	if got := getTemplateTtsVoice(template); got != "Alloy" {
		t.Fatalf("getTemplateTtsVoice() = %q, want %q", got, "Alloy")
	}
}

func TestGetTemplateTtsVoiceSkipsEmptyDefaultVoice(t *testing.T) {
	template := xiaozhiModel.AgentTemplate{
		DefaultTtsVoice: "en:",
		TtsVoices:       []string{"en:Alloy"},
	}

	if got := getTemplateTtsVoice(template); got != "Alloy" {
		t.Fatalf("getTemplateTtsVoice() = %q, want %q", got, "Alloy")
	}
}

func TestGetTemplateTtsVoiceReturnsEmptyWhenTemplateHasNoVoice(t *testing.T) {
	if got := getTemplateTtsVoice(xiaozhiModel.AgentTemplate{}); got != "" {
		t.Fatalf("getTemplateTtsVoice() = %q, want empty voice", got)
	}
}

func TestGetFirstAvailableTtsVoiceFollowsLanguageOrder(t *testing.T) {
	voice, language := getFirstAvailableTtsVoice(&xiaozhiModel.TTSList{
		Languages: []string{"zh", "en"},
		TTSVoices: map[string][]xiaozhiModel.TTSVoice{
			"zh": {{VoiceID: "wanwan"}},
			"en": {{VoiceID: "alloy"}},
		},
	})

	if voice != "wanwan" || language != "zh" {
		t.Fatalf("getFirstAvailableTtsVoice() = (%q, %q), want (%q, %q)", voice, language, "wanwan", "zh")
	}
}

func TestGetFirstAvailableTtsVoiceUsesVoiceLanguageFromResponse(t *testing.T) {
	voice, language := getFirstAvailableTtsVoice(&xiaozhiModel.TTSList{
		Languages: []string{"zh", "en"},
		TTSVoices: map[string][]xiaozhiModel.TTSVoice{
			"zh": {{
				VoiceID:  "zh_female_wanwanxiaohe_moon_bigtts",
				Language: "zh",
			}},
			"en": {{
				VoiceID:  "zh_female_shuangkuaisisi_moon_bigtts",
				Language: "en",
			}},
		},
	})

	if voice != "zh_female_wanwanxiaohe_moon_bigtts" || language != "zh" {
		t.Fatalf("getFirstAvailableTtsVoice() = (%q, %q), want Chinese voice from the API response", voice, language)
	}
}

func TestGetFirstAvailableTtsVoiceUsesFirstAvailableLanguage(t *testing.T) {
	voice, language := getFirstAvailableTtsVoice(&xiaozhiModel.TTSList{
		Languages: []string{"zh"},
		TTSVoices: map[string][]xiaozhiModel.TTSVoice{
			"zh": {{VoiceID: "wanwan"}},
		},
	})

	if voice != "wanwan" || language != "zh" {
		t.Fatalf("getFirstAvailableTtsVoice() = (%q, %q), want (%q, %q)", voice, language, "wanwan", "zh")
	}
}

func TestResolveTemplateTtsVoiceUsesVoiceIDForVoiceName(t *testing.T) {
	voice, language := resolveTemplateTtsVoice("湾湾小何", "zh", &xiaozhiModel.TTSList{
		Languages: []string{"zh"},
		TTSVoices: map[string][]xiaozhiModel.TTSVoice{
			"zh": {{VoiceID: "zh_female_wanwanxiaohe_moon_bigtts", VoiceName: "湾湾小何", Language: "zh"}},
		},
	})

	if voice != "zh_female_wanwanxiaohe_moon_bigtts" || language != "zh" {
		t.Fatalf("resolveTemplateTtsVoice() = (%q, %q), want voice ID and language", voice, language)
	}
}

func TestResolveTemplateTtsVoiceAcceptsVoiceID(t *testing.T) {
	voice, language := resolveTemplateTtsVoice("alloy-id", "en", &xiaozhiModel.TTSList{
		Languages: []string{"en"},
		TTSVoices: map[string][]xiaozhiModel.TTSVoice{
			"en": {{VoiceID: "alloy-id", VoiceName: "Alloy", Language: "en-US"}},
		},
	})

	if voice != "alloy-id" || language != "en-US" {
		t.Fatalf("resolveTemplateTtsVoice() = (%q, %q), want matching voice ID and API language", voice, language)
	}
}

func TestResolveTemplateTtsVoiceReturnsEmptyWhenVoiceDoesNotExist(t *testing.T) {
	voice, language := resolveTemplateTtsVoice("missing", "en", &xiaozhiModel.TTSList{
		Languages: []string{"en"},
		TTSVoices: map[string][]xiaozhiModel.TTSVoice{
			"en": {{VoiceID: "alloy-id", VoiceName: "Alloy", Language: "en"}},
		},
	})

	if voice != "" || language != "" {
		t.Fatalf("resolveTemplateTtsVoice() = (%q, %q), want empty result", voice, language)
	}
}
