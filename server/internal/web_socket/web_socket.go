/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

package web_socket

import (
	"context"
	"encoding/base64"
	"encoding/binary"
	"errors"
	"net"
	"net/http"
	"stackChan/internal/model"
	"stackChan/internal/service"
	"stackChan/utility"
	"strconv"
	"strings"
	"sync"
	"time"

	"github.com/gogf/gf/v2/frame/g"
	"github.com/gogf/gf/v2/net/ghttp"
	"github.com/gorilla/websocket"
)

const (
	Opus          byte = 0x01
	Jpeg          byte = 0x02
	ControlAvatar byte = 0x03
	ControlMotion byte = 0x04
	OnCamera      byte = 0x05
	OffCamera     byte = 0x06

	TextMessage byte = 0x07
	RequestCall byte = 0x09
	RefuseCall  byte = 0x0A
	AgreeCall   byte = 0x0B
	HangupCall  byte = 0x0C

	UpdateDeviceName byte = 0x0D
	GetDeviceName    byte = 0x0E

	inCall byte = 0x0F

	ping byte = 0x10
	pong byte = 0x11

	OnPhoneScreen    byte = 0x12
	OffPhoneScreen   byte = 0x13
	Dance            byte = 0x14
	GetAvatarPosture byte = 0x15

	DeviceOffline byte = 0x16
	DeviceOnline  byte = 0x17

	OnAudio  byte = 0x18
	OffAudio byte = 0x19

	AimedTakePhoto byte = 0x1A
)

var (
	wsUpGrader = websocket.Upgrader{
		CheckOrigin: func(r *http.Request) bool { return true },
		Error: func(w http.ResponseWriter, r *http.Request, status int, reason error) {
			logger.Errorf(r.Context(), "WebSocket Upgrade failed: %v", reason)
		},
	}
	logger              = g.Log()
	stackChanClientPool = sync.Map{}
	appClientPool       = sync.Map{}
	appClientMu         sync.Mutex
)

// GetMac get MAC address from request header
func GetMac(r *ghttp.Request) (string, error) {
	if token := r.Header.Get(model.Authorization); token != "" {
		decodedToken, err := base64.StdEncoding.DecodeString(token)
		if err != nil {
			logger.Errorf(r.Context(), "Error base64 decoding token: %v", err)
			return "", err
		}
		decrypted, err := utility.RSADecrypt(decodedToken)
		if err != nil {
			logger.Errorf(r.Context(), "Error decrypting token: %v", err)
			return "", err
		}
		tokenStr := string(decrypted)
		parts := strings.Split(tokenStr, "|")
		if len(parts) < 2 {
			return "", errors.New("invalid token")
		}
		mac := parts[0]
		tsStr := parts[2]
		ts, err := strconv.ParseInt(tsStr, 10, 64)
		if err != nil {
			return "", errors.New("invalid timestamp")
		}
		now := time.Now().Unix()
		if now-ts > 10 || ts-now > 10 {
			return "", errors.New("token expired or not yet valid")
		}
		return mac, nil
	}
	return "", nil
}

// Handler WebSocket handler function
func Handler(r *ghttp.Request) {
	ctx := r.Context()
	mac, err := GetMac(r)
	if err != nil || mac == "" {
		r.Response.WriteHeader(http.StatusUnauthorized) // Return 401
		r.Response.Write("Unauthorized: invalid or missing MAC")
		return
	}
	deviceType := r.Get("deviceType").String()
	if deviceType == "" {
		r.Response.Write("The mac and deviceType parameters are empty.")
		return
	}

	ws, err := wsUpGrader.Upgrade(r.Response.Writer, r.Request, nil)
	if err != nil {
		r.Response.Write(err.Error())
		return
	}

	if deviceType == "StackChan" {
		isHave := false
		var client *model.StackChanClient

		stackChanClientPool.Range(func(key, value any) bool {
			macAddr := key.(string)
			stackChanClient := value.(*model.StackChanClient)

			if macAddr == mac {
				isHave = true
				client = stackChanClient
				client.SetConn(ws)
				if client.GetCallAppClient() != nil {
					reconnectMsg := createStringMessage(TextMessage, "The equipment has been reconnected.")
					stackChanSendMessage(ctx, client, new(websocket.BinaryMessage), reconnectMsg)
				}
				if len(client.GetCameraSubscriptionList()) > 0 {
					onMsg := createMessage(OnCamera, nil)
					stackChanSendMessage(ctx, client, new(websocket.BinaryMessage), onMsg)
				}
				if len(client.GetAudioSubscriptionList()) > 0 {
					onMsg := createMessage(OnAudio, nil)
					stackChanSendMessage(ctx, client, new(websocket.BinaryMessage), onMsg)
				}
				client.SetLastTime(time.Now())
				return false
			}
			return true
		})

		if !isHave {
			client = model.NewStackChanClient(mac, ws, make([]*model.AppClient, 0), nil, false)
			addStackChenClient(ctx, client)
		}

		// send Online
		onlineMsg := createStringMessage(DeviceOnline, "Your StackChan has been launched.")
		msgType := websocket.BinaryMessage
		// Notify App
		appClients := getAppClients(client.GetMac())
		for _, appClient := range appClients {
			appSendMessage(ctx, appClient, &msgType, onlineMsg)
		}

		logger.Info(ctx, "There is a StackChen connected to the service.", client.GetMac())
		defer func() {
			logger.Info(ctx, "There is a StackChan that has disconnected.", mac, deviceType)
			if client.GetConn() != nil {
				_ = client.GetConn().Close()
				client.SetConn(nil)
			}
		}()
		for {
			messageType, msg, err := ws.ReadMessage()
			if err != nil {
				if websocket.IsCloseError(err, websocket.CloseNormalClosure, websocket.CloseGoingAway) {
					logger.Infof(ctx, "StackChan Normal disconnection: mac=%s, deviceType=%s, Reason=%v", mac, deviceType, err)
					break
				}

				if ne, ok := errors.AsType[net.Error](err); ok && ne.Temporary() {
					logger.Infof(ctx, "StackChan Temporary network error. Continue reading.: mac=%s,deviceType=%s,Error=%v", mac, deviceType, err)
					continue
				}

				logger.Errorf(ctx, "StackChan Abnormal disconnection: mac=%s, deviceType=%s, Error=%v", mac, deviceType, err)
				break
			}
			client.SetLastTime(time.Now())
			readStackChanMessage(ctx, client, &messageType, &msg)
		}
	} else if deviceType == "App" {
		deviceId := r.Get("deviceId").String()
		if deviceId == "" {
			r.Response.Write("The deviceId parameter in the App end is empty.")
			return
		}
		var client *model.AppClient
		found := false
		clients := getAppClients(mac)
		for _, appClient := range clients {
			if appClient.GetDeviceId() == deviceId && appClient.GetMac() == mac {
				// Already available. Update the connection.
				client = appClient
				client.SetConn(ws)
				client.SetLastTime(time.Now())
				found = true
				break
			}
		}
		if !found {
			client = model.NewAppClient(mac, ws, deviceId)
			addAppClient(client)
		}
		logger.Info(ctx, "There is an App connected to the service.", client.GetMac())

		// check StackChan status
		stackChanClient := getStackChanClient(client.GetMac())
		if stackChanClient == nil || stackChanClient.GetConn() == nil {
			offlineMsg := createStringMessage(DeviceOffline, "Your StackChan is offline.")
			appSendMessage(ctx, client, new(websocket.BinaryMessage), offlineMsg)
		} else {
			onlineMsg := createStringMessage(DeviceOnline, "Your StackChan has been launched.")
			appSendMessage(ctx, client, new(websocket.BinaryMessage), onlineMsg)
		}

		defer func() {
			logger.Info(ctx, "There is an App that has disconnected.", mac, deviceType)
			if client.GetConn() != nil {
				_ = client.GetConn().Close()
				client.SetConn(nil)
			}
		}()
		for {
			messageType, msg, err := ws.ReadMessage()
			if err != nil {
				var ne net.Error
				if websocket.IsCloseError(err, websocket.CloseNormalClosure, websocket.CloseGoingAway) {
					logger.Infof(ctx, "App Normal disconnection: mac=%s, deviceType=%s, Error=%v", mac, deviceType, err)
					break
				}
				if errors.As(err, &ne) && ne.Temporary() {
					logger.Infof(ctx, "App Temporary network error. Continue reading.: mac=%s,deviceType=%s,Error=%v", mac, deviceType, err)
					continue
				}
				if errors.As(err, &ne) && ne.Timeout() {
					logger.Infof(ctx, "App Timeout disconnection: mac=%s, deviceType=%s", mac, deviceType)
					break
				}
				logger.Errorf(ctx, "App Abnormal disconnection: mac=%s, deviceType=%s, Error=%v", mac, deviceType, err)
				break
			}
			client.SetLastTime(time.Now())
			readAppClientMessage(ctx, client, &messageType, &msg)
		}
	}
}

// Handle WebSocket connection requests from StackChan devices
func addStackChenClient(ctx context.Context, c *model.StackChanClient) {
	stackChanClientPool.Store(c.GetMac(), c)
	_, _ = service.CreateMacIfNotExists(ctx, c.GetMac())
}

// Handle WebSocket connection requests from App devices
func addAppClient(c *model.AppClient) {
	appClientMu.Lock()
	defer appClientMu.Unlock()

	val, _ := appClientPool.Load(c.GetMac())
	var clients []*model.AppClient
	if val != nil {
		clients = append(val.([]*model.AppClient), c)
	} else {
		clients = []*model.AppClient{c}
	}
	appClientPool.Store(c.GetMac(), clients)
}

// Get all App clients with specified MAC address
func getAppClients(mac string) []*model.AppClient {
	if val, ok := appClientPool.Load(mac); ok {
		return val.([]*model.AppClient)
	}
	return nil
}

// Get StackChan client with specified MAC address
func getStackChanClient(mac string) *model.StackChanClient {
	if val, ok := stackChanClientPool.Load(mac); ok {
		return val.(*model.StackChanClient)
	}
	return nil
}

// Parse custom binary protocol messages, return message type, data length, payload and success status
func parseBinaryMessage(ctx context.Context, msg *[]byte) (byte, int, []byte, bool) {
	if len(*msg) < 1+4 {
		logger.Warning(ctx, "Message too short, cannot parse header, message not forwarded")
		return 0, 0, nil, false
	}

	msgType := (*msg)[0]
	dataLen := int(binary.BigEndian.Uint32((*msg)[1:5]))
	payload := (*msg)[5 : 5+dataLen]

	if len(*msg)-5 != dataLen {
		logger.Warningf(ctx, "Length mismatch: header says %d, actual is %d, message not forwarded", dataLen, len(*msg)-5)
		return 0, 0, nil, false
	}

	return msgType, dataLen, payload, true
}

// Handle WebSocket messages from StackChan devices
func readStackChanMessage(ctx context.Context, client *model.StackChanClient, messageType *int, msg *[]byte) {
	if *messageType == websocket.BinaryMessage {
		msgType, _, _, ok := parseBinaryMessage(ctx, msg)
		if !ok {
			return
		}
		switch msgType {
		case pong:
			break
		case ControlAvatar, ControlMotion, OnCamera, OffCamera:
			break
		case RefuseCall:
			// Reject call, remove and notify App client
			appClient := client.GetCallAppClient()
			if appClient != nil {
				appSendMessage(ctx, appClient, messageType, msg)
				client.SetCallAppClient(nil)
			}
			break
		case AgreeCall:
			// Accept call, add App client to subscription list
			appClient := client.GetCallAppClient()
			if appClient != nil {
				appSendMessage(ctx, appClient, messageType, msg)
				client.AppendCameraSubscriptionList(appClient)
				if len(client.GetCameraSubscriptionList()) == 1 {
					onMsg := createMessage(OnCamera, nil)
					onType := websocket.BinaryMessage
					stackChanSendMessage(ctx, client, &onType, onMsg)
				}
				client.SetAudioSubscriptionList(append(client.GetAudioSubscriptionList(), appClient))
				if len(client.GetAudioSubscriptionList()) == 1 {
					onMsg := createMessage(OnAudio, nil)
					onType := websocket.BinaryMessage
					stackChanSendMessage(ctx, client, &onType, onMsg)
				}
			}
			break
		case HangupCall:
			// Hang up call, remove App client and update subscription list
			appClient := client.GetCallAppClient()
			if appClient != nil {
				appSendMessage(ctx, appClient, messageType, msg)
				// Remove the client from the subscription list
				newList := client.GetCameraSubscriptionList()[:0]
				for _, subClient := range client.GetCameraSubscriptionList() {
					if subClient != appClient {
						newList = append(newList, subClient)
					}
				}
				client.SetCameraSubscriptionList(newList)
				// If the subscription list is empty, notify to turn off the camera
				if len(client.GetCameraSubscriptionList()) == 0 {
					offMsg := createMessage(OffCamera, nil)
					offType := websocket.BinaryMessage
					stackChanSendMessage(ctx, client, &offType, offMsg)
				}

				newAudioList := client.GetAudioSubscriptionList()[:0]
				for _, subClient := range client.GetAudioSubscriptionList() {
					if subClient != appClient {
						newAudioList = append(newAudioList, subClient)
					}
				}
				client.SetAudioSubscriptionList(newAudioList)
				if len(client.GetAudioSubscriptionList()) == 0 {
					onMsg := createMessage(OnAudio, nil)
					onType := websocket.BinaryMessage
					stackChanSendMessage(ctx, client, &onType, onMsg)
				}
			}
			break
		case GetDeviceName:
			// Query device name
			name, err := service.GetDeviceName(ctx, client.GetMac())
			if err != nil {
				return
			}
			if name == "" {
				logger.Infof(ctx, "Queried device nickname is empty")
				return
			}
			newMsg := createStringMessage(GetDeviceName, name)
			stackChanSendMessage(ctx, client, messageType, newMsg)
			break
		case Opus:
			subscribers := client.GetAudioSubscriptionList()
			if len(subscribers) > 0 {
				var isAll = true
				for _, subClient := range client.GetAudioSubscriptionList() {
					if subClient.GetConn() != nil {
						isAll = false
					}
					appSendMessage(ctx, subClient, messageType, msg)
				}
				if isAll {
					msg = createMessage(OffAudio, nil)
					stackChanSendMessage(ctx, client, messageType, msg)
				}
			} else {
				msg = createMessage(OffAudio, nil)
				stackChanSendMessage(ctx, client, messageType, msg)
			}
			break
		case Jpeg:
			subscribers := client.GetCameraSubscriptionList()
			if len(subscribers) > 0 {
				var isAll = true
				for _, subClient := range subscribers {
					if subClient.GetConn() != nil {
						isAll = false
					}
					appSendMessage(ctx, subClient, messageType, msg)
				}
				if isAll {
					msg = createMessage(OffCamera, nil)
					stackChanSendMessage(ctx, client, messageType, msg)
				}
			} else {
				msg = createMessage(OffCamera, nil)
				stackChanSendMessage(ctx, client, messageType, msg)
			}
			break
		case GetAvatarPosture:
			appClients := getAppClients(client.GetMac())
			for _, appClient := range appClients {
				appSendMessage(ctx, appClient, messageType, msg)
			}
			break
		case AimedTakePhoto:
			appClient := client.GetAimedTakePhotoAppClient()
			if appClient != nil {
				appSendMessage(ctx, appClient, messageType, msg)
			}
			break
		default:
			logger.Infof(ctx, "Unknown binary msgType: %d", msgType)
			appClients := getAppClients(client.GetMac())
			if appClients != nil {
				for _, appClient := range appClients {
					appSendMessage(ctx, appClient, messageType, msg)
				}
			}
		}
	} else if *messageType == websocket.TextMessage {
		appClients := getAppClients(client.GetMac())
		if appClients != nil {
			for _, appClient := range appClients {
				appSendMessage(ctx, appClient, messageType, msg)
			}
		}
	} else if *messageType == websocket.PingMessage {
		logger.Info(ctx, "Received ping message from StackChan side")
	}
}

// Handle WebSocket messages from App clients
func readAppClientMessage(ctx context.Context, client *model.AppClient, messageType *int, msg *[]byte) {
	if *messageType == websocket.BinaryMessage {
		msgType, _, payload, ok := parseBinaryMessage(ctx, msg)
		if !ok {
			return
		}
		switch msgType {
		case pong:
			break
		case GetDeviceName:
			// Query device name
			name, err := service.GetDeviceName(ctx, client.GetMac())
			if err != nil {
				logger.Errorf(ctx, err.Error())
				return
			}
			if name == "" {
				logger.Infof(ctx, "Queried device nickname is empty")
				return
			}
			newMsg := createStringMessage(GetDeviceName, name)
			logger.Infof(ctx, "Device name found, returning: "+name)
			appSendMessage(ctx, client, messageType, newMsg)
			break
		case UpdateDeviceName:
			stackChanClient := getStackChanClient(client.GetMac())
			if stackChanClient != nil {
				stackChanSendMessage(ctx, stackChanClient, messageType, msg)
			}
			appClients := getAppClients(client.GetMac())
			for _, appClient := range appClients {
				appSendMessage(ctx, appClient, messageType, msg)
			}
			break
		case Opus:
			if payload == nil || len(payload) < 12 {
				logger.Warningf(ctx, "Payload too short, cannot parse MAC address: %v", payload)
				return
			}
			macAddrBytes := payload[:12]
			data := payload[12:]
			macAddr := string(macAddrBytes)
			newMsg := createMessage(msgType, data)
			stackChanClient := getStackChanClient(macAddr)
			if stackChanClient != nil {
				stackChanSendMessage(ctx, stackChanClient, messageType, newMsg)
			}
			break
		case Jpeg:
			if payload == nil || len(payload) < 12 {
				logger.Warningf(ctx, "Payload too short, cannot parse MAC address: %v", payload)
				return
			}
			macAddrBytes := payload[:12]
			data := payload[12:]
			macAddr := string(macAddrBytes)
			newMsg := createMessage(msgType, data)
			stackChanClient := getStackChanClient(macAddr)
			if stackChanClient != nil {
				if stackChanClient.GetPhoneScreen() {
					stackChanSendMessage(ctx, stackChanClient, messageType, newMsg)
				}
			}
			break
		case ControlAvatar, ControlMotion:
			if payload == nil || len(payload) < 12 {
				logger.Warningf(ctx, "Payload too short, cannot parse MAC address: %v", payload)
				return
			}
			macAddrBytes := payload[:12]
			data := payload[12:]
			macAddr := string(macAddrBytes)
			newMsg := createMessage(msgType, data)
			stackChanClient := getStackChanClient(macAddr)
			if stackChanClient != nil {
				stackChanSendMessage(ctx, stackChanClient, messageType, newMsg)
			} else {
				logger.Infof(ctx, "StackChan is currently offline")
			}
			break
		case TextMessage:
			if payload == nil || len(payload) < 12 {
				logger.Warningf(ctx, "Payload too short, cannot parse MAC address: %v", payload)
				return
			}
			macAddr := string(payload[:12])
			data := payload[12:]
			newMsg := createMessage(msgType, data)
			stackChanClient := getStackChanClient(macAddr)
			if stackChanClient != nil {
				stackChanSendMessage(ctx, stackChanClient, messageType, newMsg)
			}
			appClients := getAppClients(macAddr)
			if appClients != nil {
				for _, appClient := range appClients {
					appSendMessage(ctx, appClient, messageType, newMsg)
				}
			}
			break
		case RequestCall:
			// Request call
			if payload == nil || len(payload) < 12 {
				logger.Warningf(ctx, "Payload too short, cannot parse MAC address: %v", payload)
				return
			}
			macAddr := string(payload[:12])
			data := payload[12:]
			stackChanClient := getStackChanClient(macAddr)
			if stackChanClient != nil {
				if stackChanClient.GetCallAppClient() == nil || stackChanClient.GetCallAppClient() == client {
					stackChanClient.SetCallAppClient(client)
					newMsg := createMessage(msgType, data)
					stackChanSendMessage(ctx, stackChanClient, messageType, newMsg)
				} else {
					// Notify App that the other side is already in a call
					newMsg := createStringMessage(inCall, "The other party is currently in a call")
					appSendMessage(ctx, client, messageType, newMsg)
				}
			}
			break
		case HangupCall:
			stackChanClientPool.Range(func(_, value any) bool {
				stackChanClient := value.(*model.StackChanClient)
				if stackChanClient.GetCallAppClient() == client {
					// Found corresponding call
					stackChanClient.SetCallAppClient(nil)
					stackChanSendMessage(ctx, stackChanClient, messageType, msg)

					newList := stackChanClient.GetCameraSubscriptionList()[:0]
					for _, sub := range stackChanClient.GetCameraSubscriptionList() {
						if sub != client {
							newList = append(newList, sub)
						}
					}
					stackChanClient.SetCameraSubscriptionList(newList)
					if len(stackChanClient.GetCameraSubscriptionList()) == 0 {
						offMsg := createMessage(OffCamera, nil)
						offType := websocket.BinaryMessage
						stackChanSendMessage(ctx, stackChanClient, &offType, offMsg)
					}

					newAudio := stackChanClient.GetAudioSubscriptionList()[:0]
					for _, sub := range stackChanClient.GetAudioSubscriptionList() {
						if sub != client {
							newAudio = append(newAudio, sub)
						}
					}
					stackChanClient.SetAudioSubscriptionList(newAudio)
					if len(stackChanClient.GetAudioSubscriptionList()) == 0 {
						offMsg := createMessage(OffAudio, nil)
						offType := websocket.BinaryMessage
						stackChanSendMessage(ctx, stackChanClient, &offType, offMsg)
					}

					return false
				}
				return true
			})
			break
		case OnAudio:
			macAddr := string(payload)
			stackChanClient := getStackChanClient(macAddr)
			if stackChanClient != nil {
				alreadySubscribed := false
				for _, sub := range stackChanClient.GetAudioSubscriptionList() {
					if sub == client {
						alreadySubscribed = true
						break
					}
				}
				stackChanClient.SetAudioSubscriptionList(append(stackChanClient.GetAudioSubscriptionList(), client))
				if !alreadySubscribed {
					stackChanSendMessage(ctx, stackChanClient, messageType, msg)
				}
			}
			break
		case OffAudio:
			macAddr := string(payload)
			stackChanClient := getStackChanClient(macAddr)
			if stackChanClient != nil {
				existed := false
				newList := stackChanClient.GetAudioSubscriptionList()[:0]
				for _, subClient := range stackChanClient.GetAudioSubscriptionList() {
					if subClient == client {
						existed = true
					} else {
						newList = append(newList, subClient)
					}
				}
				shouldNotify := existed && len(newList) == 0
				stackChanClient.SetAudioSubscriptionList(newList)
				if shouldNotify {
					stackChanSendMessage(ctx, stackChanClient, messageType, msg)
				}
			}
			break
		case OnCamera:
			macAddr := string(payload)
			stackChanClient := getStackChanClient(macAddr)
			if stackChanClient != nil {
				for _, sub := range stackChanClient.GetCameraSubscriptionList() {
					if sub == client {
						return
					}
				}
				stackChanClient.AppendCameraSubscriptionList(client)
				stackChanSendMessage(ctx, stackChanClient, messageType, msg)
			}
			break
		case OffCamera:
			macAddr := string(payload)
			stackChanClient := getStackChanClient(macAddr)
			if stackChanClient != nil {
				existed := false
				newList := stackChanClient.GetCameraSubscriptionList()[:0]
				for _, subClient := range stackChanClient.GetCameraSubscriptionList() {
					if subClient == client {
						existed = true
					} else {
						newList = append(newList, subClient)
					}
				}
				shouldNotify := existed && len(newList) == 0
				stackChanClient.SetCameraSubscriptionList(newList)
				if shouldNotify {
					stackChanSendMessage(ctx, stackChanClient, messageType, msg)
				}
			}
			break
		case OnPhoneScreen:
			// Show phone screen
			macAddr := string(payload)
			stackChanClient := getStackChanClient(macAddr)
			if stackChanClient != nil {
				if stackChanClient.GetPhoneScreen() == false {
					stackChanClient.SetPhoneScreen(true)
					stackChanSendMessage(ctx, stackChanClient, messageType, msg)
				}
			}
			break
		case OffPhoneScreen:
			// Hide phone screen
			macAddr := string(payload)
			stackChanClient := getStackChanClient(macAddr)
			if stackChanClient != nil {
				if stackChanClient.GetPhoneScreen() == true {
					stackChanClient.SetPhoneScreen(false)
					stackChanSendMessage(ctx, stackChanClient, messageType, msg)
				}
			}
			break
		case Dance:
			// Dance message
			stackChanClient := getStackChanClient(client.GetMac())
			if stackChanClient != nil {
				stackChanSendMessage(ctx, stackChanClient, messageType, msg)
			}
			break
		case GetAvatarPosture:
			stackChanClient := getStackChanClient(client.GetMac())
			if stackChanClient != nil {
				stackChanSendMessage(ctx, stackChanClient, messageType, msg)
			}
		case AimedTakePhoto:
			stackChanClient := getStackChanClient(client.GetMac())
			if stackChanClient != nil {
				stackChanClient.SetAimedTakePhotoAppClient(client)
				stackChanSendMessage(ctx, stackChanClient, messageType, msg)
			}
			break
		default:
			logger.Infof(ctx, "Unknown binary msgType: %d", msgType)
			stackChanClient := getStackChanClient(client.GetMac())
			if stackChanClient != nil {
				stackChanSendMessage(ctx, stackChanClient, messageType, msg)
			}
		}
	} else if *messageType == websocket.TextMessage {
		// Directly forward other message types
		stackChanClient := getStackChanClient(client.GetMac())
		if stackChanClient != nil {
			stackChanSendMessage(ctx, stackChanClient, messageType, msg)
		}
	} else if *messageType == websocket.PingMessage {
		logger.Info(ctx, "Received ping message from App side")
	}
}

// Send WebSocket messages to App clients
func appSendMessage(ctx context.Context, client *model.AppClient, messageType *int, msg *[]byte) {
	select {
	case client.SendChan() <- &model.WsSendMsg{
		MsgType: *messageType,
		Data:    *msg,
	}:
	default:
		logger.Infof(ctx, "App client send message is full")
	}
}

// Send WebSocket messages to StackChan devices
func stackChanSendMessage(ctx context.Context, client *model.StackChanClient, messageType *int, msg *[]byte) {
	select {
	case client.SendChan() <- &model.WsSendMsg{
		MsgType: *messageType,
		Data:    *msg,
	}:
	default:
		logger.Infof(ctx, "StackChan client send message is full")
	}
}

// SendAppMessage Send WebSocket messages to App clients
func SendAppMessage(ctx context.Context, mac string, messageType *int, msg *[]byte, supportOfflineMode *bool) {
	clients := getAppClients(mac)
	if clients != nil {
		for _, client := range clients {
			appSendMessage(ctx, client, messageType, msg)
		}
	}
}

// SendStackChanMessage Send WebSocket messages to StackChan devices
func SendStackChanMessage(ctx context.Context, mac string, messageType *int, msg *[]byte, supportOfflineMode *bool) {
	stackChanClient := getStackChanClient(mac)
	if stackChanClient != nil {
		stackChanSendMessage(ctx, stackChanClient, messageType, msg)
	}
}

// Encapsulate binary messages for custom protocol (type + data length + data)
func createMessage(msgType byte, data []byte) *[]byte {
	var dataLen int
	if data != nil {
		dataLen = len(data)
	} else {
		dataLen = 0
	}
	msg := make([]byte, 1+4+dataLen)
	msg[0] = msgType
	binary.BigEndian.PutUint32(msg[1:5], uint32(dataLen))
	if dataLen > 0 {
		copy(msg[5:], data)
	}
	return &msg
}

// Encapsulate binary messages for custom protocol (type + data length + string data)
func createStringMessage(msgType byte, data string) *[]byte {
	return createMessage(msgType, []byte(data))
}
