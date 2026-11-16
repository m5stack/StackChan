/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

package xiaozhi

import (
	"encoding/json"
	"testing"

	"github.com/gogf/gf/v2/encoding/gjson"
)

func TestTTSListUnmarshalMatchesClientShape(t *testing.T) {
	data := []byte(`{
		"languages": ["zh", "en"],
		"tts_voices": {
			"zh": [{
				"top": true,
				"voice_id": "Cherry",
				"voice_name": "Cherry",
				"language": "zh",
				"created_at": "2026-01-02T03:04:05Z",
				"voice_demo": "https://example.com/cherry.mp3"
			}],
			"en-US": [{
				"top": false,
				"voice_id": "Alloy",
				"voice_name": "Alloy",
				"language": "en",
				"created_at": null,
				"voice_demo": null
			}]
		}
	}`)

	var list TTSList
	if err := json.Unmarshal(data, &list); err != nil {
		t.Fatalf("json.Unmarshal() error = %v", err)
	}

	if len(list.Languages) != 2 || list.Languages[0] != "zh" || list.Languages[1] != "en" {
		t.Fatalf("Languages = %#v, want [zh en]", list.Languages)
	}
	zhVoices := list.TTSVoices["zh"]
	if len(zhVoices) != 1 || !zhVoices[0].Top || zhVoices[0].VoiceID != "Cherry" {
		t.Fatalf("zh voices = %#v, want parsed Cherry voice", zhVoices)
	}
	if zhVoices[0].CreatedAt == "" || zhVoices[0].VoiceDemo == "" {
		t.Fatalf("zh voice metadata was not parsed: %#v", zhVoices[0])
	}

	enVoices := list.TTSVoices["en-US"]
	if len(enVoices) != 1 || enVoices[0].VoiceID != "Alloy" {
		t.Fatalf("en-US voices = %#v, want parsed Alloy voice", enVoices)
	}
	if enVoices[0].CreatedAt != "" || enVoices[0].VoiceDemo != "" {
		t.Fatalf("null voice metadata should be empty: %#v", enVoices[0])
	}
}

func TestTTSListScanMatchesXiaoZhiResponseShape(t *testing.T) {
	response := gjson.New(`{"success":true,"data":{"languages":["zh","en"],"tts_voices":{"zh":[{"status":1,"top":true,"voice_id":"zh_female_wanwanxiaohe_moon_bigtts","voice_name":"湾湾小何","language":"zh","created_at":"2024-12-31T16:00:00.000Z","voice_demo":"https://example.com/wanwan.mp3"}],"en":[{"status":1,"top":true,"voice_id":"zh_female_shuangkuaisisi_moon_bigtts","voice_name":"Skye (en-US)","language":"en","created_at":"2024-12-31T16:00:00.000Z","voice_demo":"https://example.com/skye.mp3"}]}}}`)

	var parsed XiaoZhiResponse[TTSList]
	if err := response.Scan(&parsed); err != nil {
		t.Fatalf("gjson.Scan() error = %v", err)
	}
	if parsed.Data == nil || len(parsed.Data.TTSVoices["zh"]) != 1 {
		t.Fatalf("parsed TTS response = %#v, want zh voice", parsed.Data)
	}
	if got := parsed.Data.TTSVoices["zh"][0].VoiceID; got != "zh_female_wanwanxiaohe_moon_bigtts" {
		t.Fatalf("voice_id = %q, want API voice_id", got)
	}
}

func TestTTSListUnmarshalDefaultsAndNulls(t *testing.T) {
	var missing TTSList
	if err := json.Unmarshal([]byte(`{}`), &missing); err != nil {
		t.Fatalf("json.Unmarshal() error = %v", err)
	}
	if len(missing.Languages) != 0 {
		t.Fatalf("Languages = %#v, want empty slice", missing.Languages)
	}
	if missing.TTSVoices != nil {
		t.Fatalf("TTSVoices = %#v, want nil when field is absent", missing.TTSVoices)
	}

	var nulls TTSList
	if err := json.Unmarshal([]byte(`{"languages": null, "tts_voices": null}`), &nulls); err != nil {
		t.Fatalf("json.Unmarshal() error = %v", err)
	}
	if len(nulls.Languages) != 0 || nulls.TTSVoices != nil {
		t.Fatalf("null fields decoded incorrectly: %#v", nulls)
	}
}
