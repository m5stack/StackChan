/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

package service

import (
	"log"
	xiaozhiModel "stackChan/internal/model/xiaozhi"
	"stackChan/internal/xiaozhi"
	"strings"
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

	// Define configuration
	var agentConfig = xiaozhiModel.AgentConfig{
		AgentName:           agentTemplate.AgentName,
		AssistantName:       agentTemplate.AssistantName,
		LlmModel:            agentTemplate.LlmModel,
		TtsVoice:            getTtsVoice("en", agentTemplate.TtsVoices),
		TtsSpeechSpeed:      agentTemplate.TtsSpeechSpeed,
		TtsPitch:            agentTemplate.TtsPitch,
		AsrSpeed:            agentTemplate.AsrSpeed,
		Language:            "en",
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

// getItsVoice Get TTS voice based on language, return pure voice name without language prefix
func getTtsVoice(language string, ttsVices []string) string {
	prefix := language + ":"
	for _, voice := range ttsVices {
		if len(voice) >= len(prefix) && voice[:len(prefix)] == prefix {
			return voice[len(prefix):]
		}
	}
	if len(ttsVices) > 0 {
		for _, voice := range ttsVices {
			if idx := strings.Index(voice, ":"); idx != -1 {
				return voice[idx+1:]
			}
		}
		return ttsVices[0]
	}
	return ""
}
