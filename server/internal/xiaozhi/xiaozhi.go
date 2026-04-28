/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

package xiaozhi

import (
	"errors"
	"fmt"
	"regexp"
	"stackChan/internal/model"
	"stackChan/internal/model/xiaozhi"
	"strconv"
	"strings"
	"sync"
	"time"

	"github.com/gogf/gf/v2/encoding/gjson"
	"github.com/gogf/gf/v2/frame/g"
	"github.com/gogf/gf/v2/net/gclient"
	"github.com/gogf/gf/v2/os/gctx"
)

var (
	ctx               = gctx.New()
	token             string       // Memory stored Token
	tokenExpire       time.Time    // Token expiration time
	mu                sync.Mutex   // Mutex lock, ensure Token update thread-safe
	ticker            *time.Ticker // 24-hour timer
	macSeparatorRegex = regexp.MustCompile(`[^0-9a-fA-F]`)
	validMacRegex     = regexp.MustCompile(`^([0-9A-Fa-f]{2}[:-]){5}([0-9A-Fa-f]{2})$`)
	globalClient      *gclient.Client // Global HTTP client
)

const (
	baseUrl            = "https://xiaozhi.me/"
	tokenPath          = "api/developers/token"
	agentTemplatesList = "api/developers/agent-templates/list"
	devices            = "api/developers/devices"
	deviceUnbind       = "api/developers/unbind-device"
	agentsDelete       = "api/agents/delete"
	createAgent        = "api/agents"
	chats              = "api/chats/list"
	tokenExpiry        = 24 * time.Hour // Token valid for 24 hours
	agents             = "api/agents"
)

func init() {
	// Initialize global HTTP client
	globalClient = g.Client()
	globalClient.SetTimeout(10 * time.Second)
	globalClient.SetHeader("Content-Type", "application/json")

	// Start 24-hour timer to periodically check and refresh Token
	ticker = time.NewTicker(tokenExpiry)
	g.Log().Info(ctx, "xiaozhi token auto refresh ticker started, refresh cycle: 24 hours")

	go func() {
		for range ticker.C {
			mu.Lock()
			// Force clear Token, next GetToken call will auto-refresh
			token = ""
			tokenExpire = time.Time{}
			mu.Unlock()
			g.Log().Info(ctx, "token expired, auto refresh")
		}
	}()
}

// Unified request processing method, auto handle Session expiration
func doRequest(method string, path string, data interface{}, resp interface{}) error {
	// Get token
	tokenString, err := GetToken()
	if err != nil {
		return err
	}

	// Clone client and set Authorization header
	client := globalClient.Clone()
	client.SetHeader("Authorization", "Bearer "+tokenString)

	var response *g.Var

	// Send request based on HTTP method
	switch strings.ToUpper(method) {
	case "GET":
		fullUrl := baseUrl + path
		if params, ok := data.(g.Map); ok && len(params) > 0 {
			var queryParts []string
			for k, v := range params {
				queryParts = append(queryParts, fmt.Sprintf("%s=%v", k, v))
			}
			queryStr := strings.Join(queryParts, "&")
			if strings.Contains(fullUrl, "?") {
				fullUrl += "&" + queryStr
			} else {
				fullUrl += "?" + queryStr
			}
		}
		response = client.GetVar(ctx, fullUrl)
	case "POST":
		response = client.PostVar(ctx, baseUrl+path, data)
	case "PUT":
		response = client.PutVar(ctx, baseUrl+path, data)
	case "DELETE":
		response = client.DeleteVar(ctx, baseUrl+path, data)
	default:
		return fmt.Errorf("unsupported request method: %s", method)
	}

	err = response.Scan(resp)
	if err != nil {
		return err
	}

	// Check if response is successful, handle Session expiration
	json := gjson.New(response.Val())
	success := json.Get("success").Bool()
	message := json.Get("message").String()
	if !success {
		if message == "Session expired or logged out" {
			g.Log().Info(ctx, "session expired or logged out, auto refresh token")
			mu.Lock()
			token = ""
			tokenExpire = time.Time{}
			mu.Unlock()
			return doRequest(method, path, data, resp)
		}
	}
	return nil
}

// GetToken Get Token (thread-safe, 24-hour auto-expiration)
func GetToken() (string, error) {
	mu.Lock()
	defer mu.Unlock()

	// 1. Check if Token exists and not expired
	if token != "" && time.Now().Before(tokenExpire) {
		g.Log().Debug(ctx, "token expired at: %s", tokenExpire.Format("2006-01-02 15:04:05"))
		return token, nil
	}

	g.Log().Info(ctx, "token expired or not exist, auto refresh")
	// 2. Token does not exist/expired, refresh to get
	newToken, err := refreshToken()
	if err != nil {
		g.Log().Error(ctx, "refresh token failed: %v", err)
		return "", err
	}

	// 3. Update Token and expiration time
	token = newToken
	tokenExpire = time.Now().Add(tokenExpiry) // Record expiration time (current time + 24 hours)
	g.Log().Info(ctx, "refresh token success, expire at: %s", tokenExpire.Format("2006-01-02 15:04:05"))

	return token, nil
}

func GetNewToken() (string, error) {
	mu.Lock()
	token = ""
	tokenExpire = time.Time{}
	mu.Unlock()
	return GetToken()
}

// DeleteAgent Delete agent
func DeleteAgent(agentId int) (bool, error) {
	params := g.Map{
		"id": agentId,
	}

	var resp xiaozhi.XiaoZhiResponse[model.Empty]
	err := doRequest("POST", agentsDelete, params, &resp)
	if err != nil {
		g.Log().Error(ctx, "delete agent failed: %v", err)
		return false, err
	}
	return true, nil
}

func CreateAgent(params g.Map) (*int, error) {
	var resp xiaozhi.XiaoZhiResponse[xiaozhi.CreateAgentResponse]
	err := doRequest("POST", createAgent, params, &resp)
	if err != nil {
		g.Log().Error(ctx, "create agent failed: %v", err)
		return nil, err
	}
	return &resp.Data.Id, nil
}

// GetAgentTemplate Get agent template
func GetAgentTemplate(page int, pageSize int) (*xiaozhi.ListData[xiaozhi.AgentTemplate], error) {
	g.Log().Debug(ctx, "Get agent template, page: ", page, " pageSize: ", pageSize)
	queryMap := g.Map{
		"page":     page,
		"pageSize": pageSize,
	}

	var resp xiaozhi.XiaoZhiResponse[xiaozhi.ListData[xiaozhi.AgentTemplate]]
	err := doRequest("GET", agentTemplatesList, queryMap, &resp)
	if err != nil {
		g.Log().Error(ctx, "Get agent template failed: %v", err)
		return nil, err
	}

	g.Log().Info(ctx,
		"Get agent template success, list length: ", len(resp.Data.List),
		" total count: ", resp.Pagination.Total,
	)

	return resp.Data, nil
}

// SetAgentSetting Update agent settings
func SetAgentSetting(agentId int, parameters xiaozhi.AgentConfig) (bool, error) {
	path := "api/agents/" + strconv.Itoa(agentId) + "/config"
	url := baseUrl + path
	g.Log().Info(ctx, "Update agent setting, agentId:", agentId, "url: ", url)
	g.Log().Info(ctx, "Request body parameters: ", gjson.MustEncodeString(parameters))

	var resp xiaozhi.XiaoZhiResponse[model.Empty]
	err := doRequest("POST", path, parameters, &resp)
	if err != nil {
		g.Log().Error(ctx, "Update agent setting failed: %v", err)
		return false, err
	}

	g.Log().Info(ctx, "Update agent setting success, agentId:", agentId)
	return true, nil
}

// GetDevices Get device list
func GetDevices(
	page *int,
	pageSize *int,
	macAddress *string,
	serialNumber *string,
	productID *int,
	DeviceID *int,
) (*[]xiaozhi.Device, error) {

	newMacAddress := formatMac(macAddress)

	// Added: Request parameter logging

	queryMap := g.Map{}
	if page != nil {
		queryMap["page"] = *page
	}
	if pageSize != nil {
		queryMap["pageSize"] = *pageSize
	}
	if macAddress != nil {
		queryMap["mac_address"] = *newMacAddress
	}
	if serialNumber != nil {
		queryMap["serial_number"] = *serialNumber
	}
	if productID != nil {
		queryMap["product_id"] = *productID
	}
	if DeviceID != nil {
		queryMap["device_id"] = *DeviceID
	}

	g.Log().Debug(ctx,
		"Get device list, data:", queryMap,
	)

	var resp xiaozhi.XiaoZhiResponse[xiaozhi.ListData[xiaozhi.Device]]
	err := doRequest("GET", devices, queryMap, &resp)
	if err != nil {
		g.Log().Error(ctx, "Get device list failed: %v", err)
		return nil, err
	}

	g.Log().Info(ctx, "Get device list success, list length:", len(resp.Data.List), " total count:", resp.Pagination.Total)

	g.Log().Info(ctx, "Get device list success, first device:", resp.Data.List[0])

	return &resp.Data.List, nil
}

func GetAgents(page *int, pageSize *int, keyword *string) (*[]xiaozhi.Agent, error) {
	// Added: Request parameter logging
	g.Log().Debug(ctx,
		"Get agent list, page:", page,
		" pageSize:", pageSize,
		" keyword:", keyword,
	)
	queryMap := g.Map{}
	if keyword != nil {
		queryMap["keyword"] = *keyword
	}
	if page != nil {
		queryMap["page"] = *page
	}
	if pageSize != nil {
		queryMap["pageSize"] = *pageSize
	}
	var resp xiaozhi.XiaoZhiResponse[[]xiaozhi.Agent]
	err := doRequest("GET", agents, queryMap, &resp)
	if err != nil {
		g.Log().Error(ctx, "Get agent list failed: %v", err)
		return nil, err
	}
	g.Log().Info(ctx,
		"Get agent list success, list length:", len(*resp.Data),
		" total count:", resp.Pagination.Total,
	)

	return resp.Data, nil
}

func formatMac(mac *string) *string {
	if mac == nil {
		return nil
	}
	cleanMac := macSeparatorRegex.ReplaceAllString(*mac, "")
	if !isValidMac(cleanMac) {
		return nil
	}
	cleanMac = strings.ToLower(cleanMac)
	var parts []string
	for i := 0; i < 12; i += 2 {
		parts = append(parts, cleanMac[i:i+2])
	}
	return new(strings.Join(parts, ":"))
}

func isValidMac(cleanMac string) bool {
	return len(cleanMac) == 12
}

type ZhiGetToken struct {
	Token string `json:"token"`
}

// refreshToken Refresh Token from server (core logic)
func refreshToken() (string, error) {
	secretKey := g.Cfg().MustGet(ctx, "xiaozhi.secret_key").String()
	if secretKey == "" {
		g.Log().Error(ctx, "xiaozhi.secret_key is empty, please check config file")
		return "", errors.New("xiaozhi.secret_key is empty")
	}

	g.Log().Debug(ctx, "refresh token")
	requestData := g.Map{
		"secret_key": secretKey,
	}

	client := g.Client()
	client.SetTimeout(10 * time.Second)
	client.SetHeader("Content-Type", "application/json")

	var resp xiaozhi.XiaoZhiResponse[ZhiGetToken]
	err := client.PostVar(ctx, baseUrl+tokenPath, requestData).Scan(&resp)
	if err != nil {
		g.Log().Error(ctx, "refresh token failed: %v", err)
		return "", fmt.Errorf("refresh token failed: %w", err)
	}

	if !resp.Success {
		g.Log().Error(ctx, "refresh token failed: %s", resp.Message)
		return "", fmt.Errorf("refresh token failed: %s", resp.Message)
	}

	// Generic structure direct value, no need for map assertion!!!
	if resp.Data.Token == "" {
		g.Log().Error(ctx, "refresh token failed: token is empty")
		return "", fmt.Errorf("token is empty")
	}

	g.Log().Debug(ctx, "refresh token success")
	return resp.Data.Token, nil
}

// UnbindDevice Unbind device from XiaoZhi side
// @param macAddress Device MAC address
func UnbindDevice(macAddress *string) (bool, error) {
	g.Log().Debug(ctx, "unbind device")

	// First query device ID
	devices, err := GetDevices(new(1), new(10), macAddress, nil, nil, nil)
	if err != nil {
		g.Log().Error(ctx, err.Error())
		return false, err
	}

	if len(*devices) == 0 {
		g.Log().Error(ctx, "unbind device failed: device not found, mac=%s", *macAddress)
		/// Device not found, return true
		return true, nil
	}
	deviceID := (*devices)[0].DeviceID

	requestData := g.Map{
		"device_id": deviceID,
	}

	g.Log().Info(ctx, "unbind device, device_id: ", (*devices)[0])
	g.Log().Info(ctx, "request data: ", gjson.MustEncodeString(requestData))

	var resp xiaozhi.XiaoZhiResponse[model.Empty]
	err = doRequest("POST", deviceUnbind, requestData, &resp)
	if err != nil {
		g.Log().Error(ctx, "unbind device failed: %v", err)
		return false, err
	}
	if !resp.Success {
		if resp.Message == "device not found" {
			g.Log().Info(ctx, "unbind device failed: device not found")
			return true, nil
		}
		g.Log().Error(ctx, "unbind device failed: %s", resp.Message)
		return false, nil
	}
	g.Log().Info(ctx, "unbind device success, device_id: ", deviceID)
	g.Log().Info(ctx, resp.Message)

	return true, nil
}

// UpdateAllDevices / Temporary script code, update mcp tools for all devices
func UpdateAllDevices() (bool, error) {
	// Initial pagination values
	page := 1
	pageSize := 100

	// Loop through pages until no more data
	for {
		// Get current page device list
		agents, err := GetAgents(&page, &pageSize, nil)
		if err != nil {
			return false, err
		}

		// If no data on current page, all pages processed, exit loop
		if agents == nil || len(*agents) == 0 {
			g.Log().Info(ctx, "update all devices success")
			break
		}

		g.Log().Info(ctx, "update all devices, page: ", page, ", total: ", len(*agents))

		for _, agent := range *agents {
			agentId := agent.ID

			parameters := xiaozhi.AgentConfig{
				AgentName:           agent.AgentName,
				AssistantName:       agent.AssistantName,
				LlmModel:            agent.LlmModel,
				TtsVoice:            agent.TtsVoice,
				TtsSpeechSpeed:      agent.TtsSpeechSpeed,
				TtsPitch:            agent.TtsPitch,
				AsrSpeed:            agent.AsrSpeed,
				Language:            agent.Language,
				Character:           agent.Character,
				Memory:              agent.Memory,
				MemoryType:          agent.MemoryType,
				KnowledgeBaseIds:    []int{},
				McpEndpoints:        nil,
				ProductMcpEndpoints: nil,
			}

			path := "api/agents/" + strconv.FormatInt(agentId, 10) + "/config"
			url := baseUrl + path
			g.Log().Info(ctx, "update agent config, agentId: ", agentId, "url: ", url)
			g.Log().Info(ctx, "request data: ", gjson.MustEncodeString(parameters))

			var resp xiaozhi.XiaoZhiResponse[model.Empty]
			err := doRequest("POST", path, parameters, &resp)
			if err != nil {
				g.Log().Error(ctx, "update agent config failed: %v", err)
				return false, err
			}

			if !resp.Success {
				g.Log().Info(ctx, "update agent config failed: %s", resp.Message)
				continue
			}

			g.Log().Info(ctx, "update agent config success, agentId: ", agentId)
			g.Log().Info(ctx, "update agent config success, agentId: ", agentId)
		}

		// Page number +1, continue requesting next page
		page++
	}

	return true, nil
}

func DeleteChats() {
	page := 1
	pageSize := 100
	requestData := g.Map{
		"page": page,
		"size": pageSize,
	}

	var resp xiaozhi.XiaoZhiResponse[xiaozhi.ListData[xiaozhi.Conversation]]
	err := doRequest("GET", chats, requestData, &resp)
	if err != nil {
		return
	}

	if !resp.Success {
		return
	}

	list := resp.Data.List

	for _, item := range list {

		url := "api/agents/" + strconv.Itoa(item.AgentId) + "/chats/" + strconv.Itoa(item.Id)

		var deleResp xiaozhi.XiaoZhiResponse[model.Empty]
		err := doRequest("DELETE", url, nil, &deleResp)
		if err != nil {
			g.Log().Error(ctx, "delete chat failed, agentId:", item.AgentId, " chatId:", item.Id, " error:", err)
			continue
		}

		g.Log().Info(ctx, "delete chat success, agentId:", item.AgentId, " chatId:", item.Id)

	}

}
