/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

package service

import (
	"log"
	"slices"
	"sort"
	"strings"

	xiaozhiModel "stackChan/internal/model/xiaozhi"
	"stackChan/internal/xiaozhi"
)

// RestoreDefaultAgent / Restore to template agent when unbinding
func RestoreDefaultAgent(mac string) (bool, error) {
	// Entry log
	log.Printf("[RestoreDefaultAgent] Start restoring device default agent configuration, mac=%s", mac)

	/// First query device information
	devices, err := xiaozhi.GetDevices(nil, nil, &mac, nil, nil, nil)
	if err != nil {
		log.Printf("[RestoreDefaultAgent] Failed to query device information, mac=%s, err=%v", mac, err)
		return false, err
	}

	if len(*devices) == 0 {
		log.Printf("[RestoreDefaultAgent] No device found, mac=%s", mac)
		return false, nil
	}

	agentID := (*devices)[0].AgentID

	return RestoreDefaultAgentByAgentID(agentID, mac)
}

func RestoreDefaultAgentByAgentID(agentID int, mac string) (bool, error) {
	if agentID == 0 {
		log.Printf("[RestoreDefaultAgent] Device agentID is empty, mac=%s", mac)
		return false, nil
	}

	// Fix here: agentID is int -> %d
	log.Printf("[RestoreDefaultAgent] Found device agentID=%d, mac=%s", agentID, mac)

	// Get default template
	response, err := xiaozhi.GetAgentTemplate(1, 10)
	if err != nil {
		// Fix
		log.Printf("[RestoreDefaultAgent] Failed to get agent template, agentID=%d, mac=%s, err=%v", agentID, mac, err)
		return false, err
	}

	if len(response.List) == 0 {
		// Fix
		log.Printf("[RestoreDefaultAgent] Agent template list is empty, agentID=%d, mac=%s", agentID, mac)
		return false, nil
	}

	agentTemplate := response.List[0]
	log.Printf("[RestoreDefaultAgent] Got default agent template, templateName=%s, model=%s",
		agentTemplate.AgentName, agentTemplate.LlmModel)
	templateVoice, templateLanguage := getTemplateTtsVoiceAndLanguage(agentTemplate)
	voiceList, err := xiaozhi.GetTTSList()
	if err != nil {
		log.Printf("[RestoreDefaultAgent] Failed to get TTS voice list, agentID=%d, mac=%s, err=%v", agentID, mac, err)
		return false, err
	}
	ttsVoice, language := resolveTemplateTtsVoice(templateVoice, templateLanguage, voiceList)
	if ttsVoice == "" {
		ttsVoice, language = getFirstAvailableTtsVoice(voiceList)
	}
	if ttsVoice == "" {
		log.Printf("[RestoreDefaultAgent] No usable TTS voice is available, agentID=%d, mac=%s", agentID, mac)
		return false, nil
	}

	// Define configuration
	var agentConfig = xiaozhiModel.AgentConfig{
		AgentName:           agentTemplate.AgentName,
		AssistantName:       agentTemplate.AssistantName,
		LlmModel:            agentTemplate.LlmModel,
		TtsVoice:            ttsVoice,
		TtsSpeechSpeed:      agentTemplate.TtsSpeechSpeed,
		TtsPitch:            agentTemplate.TtsPitch,
		AsrSpeed:            agentTemplate.AsrSpeed,
		Language:            language,
		Character:           agentTemplate.Character,
		Memory:              "",
		MemoryType:          "OFF",
		KnowledgeBaseIds:    agentTemplate.KnowledgeBaseIds,
		McpEndpoints:        nil,
		ProductMcpEndpoints: nil,
	}
	// Start update
	return xiaozhi.SetAgentSetting(agentID, agentConfig)
}

func getTemplateTtsVoice(template xiaozhiModel.AgentTemplate) string {
	voice, _ := getTemplateTtsVoiceAndLanguage(template)
	return voice
}

func getTemplateTtsVoiceAndLanguage(template xiaozhiModel.AgentTemplate) (string, string) {
	if voice, language := parseTtsVoice(template.DefaultTtsVoice, "en"); voice != "" {
		return voice, language
	}

	var fallbackVoice, fallbackLanguage string
	for _, rawVoice := range template.TtsVoices {
		voice, language := parseTtsVoice(rawVoice, "en")
		if voice == "" {
			continue
		}
		if language == "en" {
			return voice, language
		}
		if fallbackVoice == "" {
			fallbackVoice, fallbackLanguage = voice, language
		}
	}
	return fallbackVoice, fallbackLanguage
}

func getFirstAvailableTtsVoice(voiceList *xiaozhiModel.TTSList) (string, string) {
	if voiceList == nil {
		return "", ""
	}

	languages := make([]string, 0, len(voiceList.Languages)+len(voiceList.TTSVoices))
	addLanguage := func(language string) {
		if language == "" {
			return
		}
		if slices.Contains(languages, language) {
			return
		}
		languages = append(languages, language)
	}
	for _, language := range voiceList.Languages {
		addLanguage(language)
	}

	remaining := make([]string, 0, len(voiceList.TTSVoices))
	for language := range voiceList.TTSVoices {
		remaining = append(remaining, language)
	}
	sort.Strings(remaining)
	for _, language := range remaining {
		addLanguage(language)
	}

	for _, language := range languages {
		for _, voice := range voiceList.TTSVoices[language] {
			voiceID := strings.TrimSpace(voice.VoiceID)
			if voiceID == "" {
				continue
			}
			voiceLanguage := strings.TrimSpace(voice.Language)
			if voiceLanguage == "" {
				voiceLanguage = language
			}
			return voiceID, voiceLanguage
		}
	}
	return "", ""
}

// resolveTemplateTtsVoice converts a template voice name or ID to the voice ID
// expected by the agent configuration endpoint.
func resolveTemplateTtsVoice(templateVoice, templateLanguage string, voiceList *xiaozhiModel.TTSList) (string, string) {
	templateVoice = strings.TrimSpace(templateVoice)
	if templateVoice == "" || voiceList == nil {
		return "", ""
	}

	languages := make([]string, 0, len(voiceList.Languages)+len(voiceList.TTSVoices))
	addLanguage := func(language string) {
		language = strings.TrimSpace(language)
		if language == "" || slices.Contains(languages, language) {
			return
		}
		languages = append(languages, language)
	}
	addLanguage(templateLanguage)
	for _, language := range voiceList.Languages {
		addLanguage(language)
	}
	remaining := make([]string, 0, len(voiceList.TTSVoices))
	for language := range voiceList.TTSVoices {
		remaining = append(remaining, language)
	}
	sort.Strings(remaining)
	for _, language := range remaining {
		addLanguage(language)
	}

	for _, language := range languages {
		for _, voice := range voiceList.TTSVoices[language] {
			voiceID := strings.TrimSpace(voice.VoiceID)
			if voiceID == "" {
				continue
			}
			voiceName := strings.TrimSpace(voice.VoiceName)
			if templateVoice != voiceID && templateVoice != voiceName {
				continue
			}
			voiceLanguage := strings.TrimSpace(voice.Language)
			if voiceLanguage == "" {
				voiceLanguage = language
			}
			return voiceID, voiceLanguage
		}
	}
	return "", ""
}

func parseTtsVoice(rawVoice, defaultLanguage string) (string, string) {
	rawVoice = strings.TrimSpace(rawVoice)
	if rawVoice == "" {
		return "", ""
	}
	language := defaultLanguage
	voiceID := rawVoice
	if separator := strings.Index(rawVoice, ":"); separator >= 0 {
		if parsedLanguage := strings.TrimSpace(rawVoice[:separator]); parsedLanguage != "" {
			language = parsedLanguage
		}
		voiceID = strings.TrimSpace(rawVoice[separator+1:])
	}
	if voiceID == "" {
		return "", ""
	}
	return voiceID, language
}
