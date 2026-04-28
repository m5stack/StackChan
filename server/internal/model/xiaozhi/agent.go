/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

package xiaozhi

import "time"

type XiaoZhiResponse[T any] struct {
	Success    bool       `json:"success"`
	Data       *T         `json:"data"`
	Message    string     `json:"message"`
	Pagination Pagination `json:"pagination"`
	Token      string     `json:"token"`
	Code       string     `json:"code"`
}

type Pagination struct {
	Total     int  `json:"total"`
	Current   int  `json:"current"`
	PageSize  int  `json:"pageSize"`
	HasMore   bool `json:"hasMore"`
	Page      int  `json:"page"`
	Limit     int  `json:"limit"`
	TotaPages int  `json:"totaPages"`
}

type ListData[T any] struct {
	List       []T        `json:"list"`
	Pagination Pagination `json:"pagination"`
}

type AgentTemplate struct {
	Id               int      `json:"id"`
	DeveloperId      int      `json:"developer_id"`
	AgentName        string   `json:"agent_name"`
	TtsVoices        []string `json:"tts_voices"`
	DefaultTtsVoice  string   `json:"default_tts_voice"`
	LlmModel         string   `json:"llm_model"`
	AssistantName    string   `json:"assistant_name"`
	UserName         string   `json:"user_name"`
	CreatedAt        string   `json:"created_at"`
	UpdatedAt        string   `json:"updated_at"`
	Character        string   `json:"character"`
	TtsSpeechSpeed   string   `json:"tts_speech_speed"`
	AsrSpeed         string   `json:"asr_speed"`
	TtsPitch         int      `json:"tts_pitch"`
	KnowledgeBaseIds []int    `json:"knowledge_base_ids"`
	XiaoZhiVersion   string   `json:"xiao_zhi_version"`
	TtsVoiceName     string   `json:"tts_voice_name"`
}

type AgentConfig struct {
	AgentName           string   `json:"agent_name"`
	AssistantName       string   `json:"assistant_name"`
	LlmModel            string   `json:"llm_model"`
	TtsVoice            string   `json:"tts_voice"`
	TtsSpeechSpeed      string   `json:"tts_speech_speed"`
	TtsPitch            int      `json:"tts_pitch"`
	AsrSpeed            string   `json:"asr_speed"`
	Language            string   `json:"language"`
	Character           string   `json:"character"`
	Memory              string   `json:"memory"`
	MemoryType          string   `json:"memory_type"`
	KnowledgeBaseIds    []int    `json:"knowledge_base_ids"`
	McpEndpoints        []string `json:"mcp_endpoints"`
	ProductMcpEndpoints []string `json:"product_mcp_endpoints"`
}

type CreateAgentResponse struct {
	Id int `json:"id"`
}

// Agent class
type Agent struct {
	ID                  int64      `json:"id"`
	UserID              int64      `json:"user_id"`
	AgentName           string     `json:"agent_name"`
	TtsVoice            string     `json:"tts_voice"`
	LlmModel            string     `json:"llm_model"`
	AssistantName       string     `json:"assistant_name"`
	UserName            string     `json:"user_name"`
	CreatedAt           time.Time  `json:"created_at"`
	UpdatedAt           time.Time  `json:"updated_at"`
	Memory              string     `json:"memory"`
	Character           string     `json:"character"`
	LongMemorySwitch    int        `json:"long_memory_switch"`
	LangCode            string     `json:"lang_code"`
	Language            string     `json:"language"`
	TtsSpeechSpeed      string     `json:"tts_speech_speed"`
	AsrSpeed            string     `json:"asr_speed"`
	TtsPitch            int        `json:"tts_pitch"`
	AgentTemplateID     int64      `json:"agent_template_id"`
	MemoryUpdatedAt     time.Time  `json:"memory_updated_at"`
	ShareAgentID        *int64     `json:"share_agent_id"`
	Source              string     `json:"source"`
	McpEndpoints        []string   `json:"mcp_endpoints"`
	MemoryType          string     `json:"memory_type"`
	KnowledgeBaseIDs    []int64    `json:"knowledge_base_ids"`
	MaxMessageCount     *int64     `json:"max_message_count"`
	ProductMcpEndpoints []string   `json:"product_mcp_endpoints"`
	DeviceCount         int        `json:"deviceCount"`
	LastDevice          LastDevice `json:"lastDevice"`
}

// LastDevice nested device struct
type LastDevice struct {
	ID              int64     `json:"id"`
	UserID          int64     `json:"user_id"`
	MacAddress      string    `json:"mac_address"`
	CreatedAt       time.Time `json:"created_at"`
	UpdatedAt       time.Time `json:"updated_at"`
	LastConnectedAt time.Time `json:"last_connected_at"`
	AutoUpdate      int       `json:"auto_update"`
	Alias           *string   `json:"alias"`
	AgentID         int64     `json:"agent_id"`
}
