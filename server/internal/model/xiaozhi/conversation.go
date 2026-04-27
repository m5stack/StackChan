/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

package xiaozhi

type Conversation struct {
	Id          int         `json:"id"`
	UserId      int         `json:"user_id"`
	CreatedAt   string      `json:"created_at"`
	DeviceId    int         `json:"device_id"`
	MsgCount    int         `json:"msg_count"`
	AgentId     int         `json:"agent_id"`
	Model       string      `json:"model"`
	TokenCount  int         `json:"token_count"`
	Duration    int         `json:"duration"`
	ChatSummary ChatSummary `json:"chat_summary"`
}

type ChatSummary struct {
	Title   string `json:"title"`
	Summary string `json:"summary"`
}
