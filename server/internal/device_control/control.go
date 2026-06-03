/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

package device_control

import (
	"context"
	"fmt"
	"net"
	"net/http"
	"net/url"
	"os"
	"stackChan/internal/dao"
	"stackChan/internal/model"
	"strings"
	"sync"
	"time"

	"github.com/gogf/gf/v2/frame/g"
	"github.com/gogf/gf/v2/net/ghttp"
	"github.com/gorilla/websocket"
)

const (
	controlWsPort         = 8080
	controlWsPath         = "/ws"
	controlWsDefaultToken = "stackchan-local-dev"
	controlPresenceTTL    = 5 * time.Minute
	controlCloseTimeout   = 2 * time.Second
)

type apiResponse struct {
	Code    int    `json:"code"`
	Message string `json:"message"`
	Data    any    `json:"data"`
}

type Presence struct {
	Mac         string    `json:"mac"`
	IP          string    `json:"ip"`
	ControlURL  string    `json:"control_url"`
	WifiPresent bool      `json:"wifi_present"`
	VoiceActive bool      `json:"voice_active"`
	LastSeen    time.Time `json:"last_seen"`
}

var (
	presenceByMac = sync.Map{}
	upgrader      = websocket.Upgrader{
		CheckOrigin: func(r *http.Request) bool { return true },
	}
)

func UpdatePresence(mac string, remoteAddr string, voiceActive bool) {
	mac = strings.TrimSpace(mac)
	ip := remoteIP(remoteAddr)
	if mac == "" || ip == "" {
		return
	}

	presenceByMac.Store(mac, Presence{
		Mac:         mac,
		IP:          ip,
		ControlURL:  controlURL(ip),
		WifiPresent: true,
		VoiceActive: voiceActive,
		LastSeen:    time.Now(),
	})
}

func SetVoiceActive(mac string, active bool) {
	if value, ok := presenceByMac.Load(mac); ok {
		presence := value.(Presence)
		presence.VoiceActive = active
		presence.LastSeen = time.Now()
		presence.WifiPresent = true
		presenceByMac.Store(mac, presence)
	}
}

func StatusHandler(r *ghttp.Request) {
	ctx := r.Context()
	mac := strings.TrimSpace(r.Get("mac").String())
	if mac == "" {
		writeJSONError(r, http.StatusBadRequest, "mac is required")
		return
	}

	if err := authorizeUserDevice(ctx, mac); err != nil {
		writeJSONError(r, http.StatusForbidden, err.Error())
		return
	}

	presence, ok := getPresence(mac)
	if !ok {
		writeJSONError(r, http.StatusNotFound, "device control presence is unknown")
		return
	}

	r.Response.WriteJsonExit(apiResponse{
		Code:    http.StatusOK,
		Message: "ok",
		Data:    presence,
	})
}

func ProxyHandler(r *ghttp.Request) {
	ctx := r.Context()
	mac := strings.TrimSpace(r.Get("mac").String())
	if mac == "" {
		writeJSONError(r, http.StatusBadRequest, "mac is required")
		return
	}

	if err := authorizeUserDevice(ctx, mac); err != nil {
		writeJSONError(r, http.StatusForbidden, err.Error())
		return
	}

	presence, ok := getPresence(mac)
	if !ok {
		writeJSONError(r, http.StatusNotFound, "device control presence is unknown")
		return
	}

	browserConn, err := upgrader.Upgrade(r.Response.Writer, r.Request, nil)
	if err != nil {
		g.Log().Errorf(ctx, "control proxy browser upgrade failed: %v", err)
		return
	}
	defer closeWebSocket(browserConn)

	deviceConn, _, err := websocket.DefaultDialer.Dial(deviceControlURL(presence.IP), nil)
	if err != nil {
		g.Log().Errorf(ctx, "control proxy device dial failed: mac=%s ip=%s err=%v", mac, presence.IP, err)
		_ = browserConn.WriteMessage(websocket.TextMessage, []byte(`{"type":"error","message":"device control websocket unavailable"}`))
		return
	}
	defer closeWebSocket(deviceConn)

	done := make(chan error, 2)
	go proxyWebSocketMessages(deviceConn, browserConn, done)
	go proxyWebSocketMessages(browserConn, deviceConn, done)

	err = <-done
	if err != nil && !websocket.IsCloseError(err, websocket.CloseNormalClosure, websocket.CloseGoingAway) {
		g.Log().Debugf(ctx, "control proxy closed: mac=%s err=%v", mac, err)
	}
}

func getPresence(mac string) (Presence, bool) {
	value, ok := presenceByMac.Load(mac)
	if !ok {
		return Presence{}, false
	}

	presence := value.(Presence)
	presence.WifiPresent = time.Since(presence.LastSeen) <= controlPresenceTTL
	presence.ControlURL = controlURL(presence.IP)
	return presence, true
}

func authorizeUserDevice(ctx context.Context, mac string) error {
	uid := g.RequestFromCtx(ctx).GetCtxVar(model.Uid).Int64()
	if uid == 0 {
		return fmt.Errorf("user uid is required")
	}

	count, err := dao.Device.Ctx(ctx).Where("mac = ? AND uid = ?", mac, uid).Count()
	if err != nil {
		return err
	}
	if count == 0 {
		return fmt.Errorf("device not found or not bound to current user")
	}
	return nil
}

func remoteIP(remoteAddr string) string {
	host, _, err := net.SplitHostPort(remoteAddr)
	if err == nil {
		return host
	}
	return strings.TrimSpace(remoteAddr)
}

func controlURL(ip string) string {
	if ip == "" {
		return ""
	}
	return fmt.Sprintf("ws://%s:%d%s", ip, controlWsPort, controlWsPath)
}

func deviceControlURL(ip string) string {
	return fmt.Sprintf("%s?token=%s", controlURL(ip), url.QueryEscape(controlWsToken()))
}

func controlWsToken() string {
	token := strings.TrimSpace(os.Getenv("STACKCHAN_CONTROL_WS_TOKEN"))
	if token == "" {
		return controlWsDefaultToken
	}
	return token
}

func proxyWebSocketMessages(dst *websocket.Conn, src *websocket.Conn, done chan<- error) {
	for {
		messageType, data, err := src.ReadMessage()
		if err != nil {
			done <- err
			return
		}
		if err := dst.WriteMessage(messageType, data); err != nil {
			done <- err
			return
		}
	}
}

func closeWebSocket(conn *websocket.Conn) {
	if conn == nil {
		return
	}
	_ = conn.WriteControl(websocket.CloseMessage, websocket.FormatCloseMessage(websocket.CloseNormalClosure, ""), time.Now().Add(controlCloseTimeout))
	_ = conn.Close()
}

func writeJSONError(r *ghttp.Request, status int, message string) {
	r.Response.WriteStatusExit(status, apiResponse{
		Code:    status,
		Message: message,
		Data:    nil,
	})
}
