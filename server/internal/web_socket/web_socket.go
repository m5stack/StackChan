/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

package web_socket

import (
	"context"
	"encoding/binary"
	"errors"
	"math/rand"
	"net"
	"net/http"
	"stackChan/internal/service"
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
)

// AppClient indicates a WebSocket client connection on the App side
type AppClient struct {
	Mac      string
	Conn     *websocket.Conn
	mu       *sync.RWMutex
	DeviceId string
	LastTime time.Time
}

// StackChanClient indicates a WebSocket client connection for the device end of a StackChan
type StackChanClient struct {
	Mac                    string
	Conn                   *websocket.Conn
	mu                     *sync.RWMutex
	CameraSubscriptionList []*AppClient
	CallAppClient          *AppClient
	phoneScreen            bool
	LastTime               time.Time
}

func Handler(r *ghttp.Request) {
	ctx := r.Context()
	mac := r.Get("mac").String()
	deviceType := r.Get("deviceType").String()
	if mac == "" || deviceType == "" {
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
		var client *StackChanClient

		stackChanClientPool.Range(func(key, value any) bool {
			macAddr := key.(string)
			stackChanClient := value.(*StackChanClient)

			if macAddr == mac {
				isHave = true
				client = stackChanClient
				client.mu.Lock()
				client.Conn = ws
				if client.CallAppClient != nil {
					reconnectMsg := createStringMessage(TextMessage, "The equipment has been reconnected.")
					msgType := websocket.BinaryMessage
					forwardMessage(ctx, client.CallAppClient.Conn, &msgType, reconnectMsg, client.CallAppClient.mu)
				}
				if len(client.CameraSubscriptionList) > 0 {
					onMsg := createMessage(OnCamera, nil)
					onType := websocket.BinaryMessage
					forwardMessage(ctx, client.Conn, &onType, onMsg, client.mu)
				}
				client.LastTime = time.Now()
				client.mu.Unlock()
				return false
			}
			return true
		})

		if !isHave {
			client = &StackChanClient{
				Mac:         mac,
				Conn:        ws,
				mu:          &sync.RWMutex{},
				phoneScreen: false,
				LastTime:    time.Now(),
			}
			addStackChenClient(ctx, client)
		} else {
			// notify app
			onlineMsg := createStringMessage(DeviceOnline, "Your StackChan has been launched.")
			msgType := websocket.BinaryMessage
			// Notify App
			appClients := getAppClients(client.Mac)
			for _, appClient := range appClients {
				forwardMessage(ctx, appClient.Conn, &msgType, onlineMsg, appClient.mu)
			}
		}
		logger.Info(ctx, "There is a StackChen connected to the service.", client.Mac)
		defer func() {
			logger.Info(ctx, "There is a StackChan that has disconnected.", mac, deviceType)
		}()
		for {
			messageType, msg, err := ws.ReadMessage()
			if err != nil {
				if websocket.IsCloseError(err, websocket.CloseNormalClosure, websocket.CloseGoingAway) {
					logger.Infof(ctx, "StackChan Normal disconnection: mac=%s, deviceType=%s, Reason=%v", mac, deviceType, err)
					break
				}

				var ne net.Error
				if errors.As(err, &ne) && ne.Temporary() {
					logger.Infof(ctx, "StackChan Temporary network error. Continue reading.: mac=%s,deviceType=%s,Error=%v", mac, deviceType, err)
					continue
				}

				logger.Errorf(ctx, "StackChan Abnormal disconnection: mac=%s, deviceType=%s, Error=%v", mac, deviceType, err)
				break
			}
			//logger.Infof(ctx, "收到StackChan端消息%d", len(msg))
			readStackChanMessage(ctx, client, &messageType, &msg)
		}
	} else if deviceType == "App" {
		deviceId := r.Get("deviceId").String()
		if deviceId == "" {
			r.Response.Write("The deviceId parameter in the App end is empty.")
			return
		}
		var client *AppClient
		found := false
		clients := getAppClients(mac)
		for _, appClient := range clients {
			if appClient.DeviceId == deviceId && appClient.Mac == mac {
				// Already available. Update the connection.
				client = appClient
				client.mu.Lock()
				client.Conn = ws
				client.mu.Unlock()
				client.LastTime = time.Now()
				found = true
				break
			}
		}
		if !found {
			client = &AppClient{
				Mac:      mac,
				Conn:     ws,
				DeviceId: deviceId,
				mu:       &sync.RWMutex{},
				LastTime: time.Now(),
			}
			addAppClient(client)
		}
		logger.Info(ctx, "There is an App connected to the service.", client.Mac)

		// check StackChan status
		stackChanClient := getStackChanClient(client.Mac)
		if stackChanClient == nil {
			offlineMsg := createStringMessage(DeviceOffline, "Your StackChan is offline.")
			msgType := websocket.BinaryMessage
			forwardMessage(ctx, client.Conn, &msgType, offlineMsg, client.mu)
		} else {
			onlineMsg := createStringMessage(DeviceOnline, "Your StackChan has been launched.")
			msgType := websocket.BinaryMessage
			forwardMessage(ctx, client.Conn, &msgType, onlineMsg, client.mu)
		}

		defer func() {
			logger.Info(ctx, "There is an App that has disconnected.", mac, deviceType)
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
			client.LastTime = time.Now()
			readAppClientMessage(ctx, client, &messageType, &msg)
		}
	}
}

// addStackChenClient adds a StackChan client to the connection pool and ensures the MAC is registered
func addStackChenClient(ctx context.Context, c *StackChanClient) {
	stackChanClientPool.Store(c.Mac, c)
	_, _ = service.CreateMacIfNotExists(ctx, c.Mac)
}

// addAppClient adds an App client to the App connection pool (multiple Apps per MAC allowed)
func addAppClient(c *AppClient) {
	val, _ := appClientPool.Load(c.Mac)
	var clients []*AppClient
	if val == nil {
		clients = []*AppClient{c}
	} else {
		clients = val.([]*AppClient)
		clients = append(clients, c)
	}
	appClientPool.Store(c.Mac, clients)
}

// getAppClients gets all App clients for the specified MAC address
func getAppClients(mac string) []*AppClient {
	if val, ok := appClientPool.Load(mac); ok {
		return val.([]*AppClient)
	}
	return nil
}

// getStackChanClient gets the StackChan client corresponding to the specified MAC address
func getStackChanClient(mac string) *StackChanClient {
	if val, ok := stackChanClientPool.Load(mac); ok {
		return val.(*StackChanClient)
	}
	return nil
}

// parseBinaryMessage parses a custom binary protocol message, returns type, length, payload, and success status
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

// StartPingTime sends Ping messages to all connected clients for heartbeat detection
func StartPingTime(ctx context.Context) {
	message := createMessage(ping, nil)
	messageType := websocket.BinaryMessage

	// Iterate over StackChanClientPool
	stackChanClientPool.Range(func(_, value any) bool {
		client := value.(*StackChanClient)
		forwardMessage(ctx, client.Conn, &messageType, message, client.mu)
		return true // continue iteration
	})

	// Iterate over AppClientPool
	appClientPool.Range(func(_, value any) bool {
		clients := value.([]*AppClient)
		for _, client := range clients {
			forwardMessage(ctx, client.Conn, &messageType, message, client.mu)
		}
		return true // continue iteration
	})
}

// CheckExpiredLinks checks and cleans up App client connections that have been inactive for over 60 seconds
func CheckExpiredLinks(ctx context.Context) {
	now := time.Now()
	var expiredClients []*AppClient

	// First, iterate over AppClientPool
	appClientPool.Range(func(mac, value any) bool {
		clients := value.([]*AppClient)
		newClients := clients[:0]
		for _, client := range clients {
			if now.Sub(client.LastTime) > time.Second*15 {
				// Found expired client
				// Iterate over StackChanClientPool to clean up CallAppClient and CameraSubscriptionList
				stackChanClientPool.Range(func(_, scValue any) bool {
					stackChanClient, ok := scValue.(*StackChanClient)
					stackChanClient.mu.Lock()
					if !ok {
						return true
					}
					// Clean up CallAppClient
					if stackChanClient.CallAppClient == client {
						stackChanClient.CallAppClient = nil
					}

					// Update camera subscription list
					newCamera := stackChanClient.CameraSubscriptionList[:0]
					removedCamera := false
					for _, sub := range stackChanClient.CameraSubscriptionList {
						if sub != client {
							newCamera = append(newCamera, sub)
						} else {
							removedCamera = true
						}
					}
					stackChanClient.CameraSubscriptionList = newCamera
					stackChanClient.mu.Unlock()
					if removedCamera && len(newCamera) == 0 {
						msg := createMessage(OffCamera, nil)
						msgType := websocket.BinaryMessage
						forwardMessage(ctx, stackChanClient.Conn, &msgType, msg, stackChanClient.mu)
					}
					return true
				})
				expiredClients = append(expiredClients, client)
			} else {
				newClients = append(newClients, client)
			}
		}
		if len(newClients) == 0 {
			appClientPool.Delete(mac)
		} else {
			appClientPool.Store(mac, newClients)
		}
		return true
	})

	for _, client := range expiredClients {
		logger.Infof(ctx, "Kicked out an expired App client: %s", client.Mac)
		err := client.Conn.Close()
		if err != nil {
		}
	}
}

// readStackChanMessage handles messages from the StackChan device side
func readStackChanMessage(ctx context.Context, client *StackChanClient, messageType *int, msg *[]byte) {
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
			// Refused call, remove and notify appClient
			appClient := client.CallAppClient
			if appClient != nil {
				forwardMessage(ctx, appClient.Conn, messageType, msg, appClient.mu)
				client.mu.Lock()
				client.CallAppClient = nil
				client.mu.Unlock()
			}
			break
		case AgreeCall:
			// Agreed to call
			appClient := client.CallAppClient
			if appClient != nil {
				forwardMessage(ctx, appClient.Conn, messageType, msg, appClient.mu)
				client.mu.Lock()
				client.CameraSubscriptionList = append(client.CameraSubscriptionList, appClient)
				client.mu.Unlock()
				if len(client.CameraSubscriptionList) == 1 {
					onMsg := createMessage(OnCamera, nil)
					onType := websocket.BinaryMessage
					forwardMessage(ctx, client.Conn, &onType, onMsg, client.mu)
				}
			}
			break
		case HangupCall:
			// Hang up call
			appClient := client.CallAppClient
			if appClient != nil {
				forwardMessage(ctx, appClient.Conn, messageType, msg, appClient.mu)
				// Remove the client from the subscription list
				newList := client.CameraSubscriptionList[:0]
				for _, subClient := range client.CameraSubscriptionList {
					if subClient != appClient {
						newList = append(newList, subClient)
					}
				}
				client.mu.Lock()
				client.CameraSubscriptionList = newList
				client.mu.Unlock()
				// If the subscription list is empty, notify to turn off the camera
				if len(client.CameraSubscriptionList) == 0 {
					offMsg := createMessage(OffCamera, nil)
					offType := websocket.BinaryMessage
					forwardMessage(ctx, client.Conn, &offType, offMsg, client.mu)
				}
			}
			break
		case GetDeviceName:
			// Query device name
			name, err := service.GetDeviceName(ctx, client.Mac)
			if err != nil {
				return
			}
			if name == "" {
				logger.Infof(ctx, "Queried device nickname is empty")
				return
			}
			newMsg := createStringMessage(GetDeviceName, name)
			forwardMessage(ctx, client.Conn, messageType, newMsg, client.mu)
			break
		case Opus:

			break
		case Jpeg:
			subscribers := client.CameraSubscriptionList
			if len(subscribers) > 0 {
				var isAll = true
				for _, subClient := range subscribers {
					if subClient.Conn != nil {
						isAll = false
					}
					forwardMessage(ctx, subClient.Conn, messageType, msg, subClient.mu)
				}
				if isAll {
					msg = createMessage(OffCamera, nil)
					forwardMessage(ctx, client.Conn, messageType, msg, client.mu)
				}
			} else {
				msg = createMessage(OffCamera, nil)
				forwardMessage(ctx, client.Conn, messageType, msg, client.mu)
			}
			break
		case GetAvatarPosture:
			appClients := getAppClients(client.Mac)
			for _, appClient := range appClients {
				forwardMessage(ctx, appClient.Conn, messageType, msg, appClient.mu)
			}
			break
		default:
			logger.Infof(ctx, "Unknown binary msgType: %d", msgType)
			appClients := getAppClients(client.Mac)
			if appClients != nil {
				for _, appClient := range appClients {
					forwardMessage(ctx, appClient.Conn, messageType, msg, appClient.mu)
				}
			}
		}
	} else if *messageType == websocket.TextMessage {
		appClients := getAppClients(client.Mac)
		if appClients != nil {
			for _, appClient := range appClients {
				forwardMessage(ctx, appClient.Conn, messageType, msg, appClient.mu)
			}
		}
	} else if *messageType == websocket.PingMessage {
		logger.Info(ctx, "Received ping message from StackChan side")
	}
}

// readAppClientMessage handles messages from the App side
func readAppClientMessage(ctx context.Context, client *AppClient, messageType *int, msg *[]byte) {
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
			name, err := service.GetDeviceName(ctx, client.Mac)
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
			forwardMessage(ctx, client.Conn, messageType, newMsg, client.mu)
			break
		case UpdateDeviceName:
			stackChanClient := getStackChanClient(client.Mac)
			if stackChanClient != nil {
				forwardMessage(ctx, stackChanClient.Conn, messageType, msg, stackChanClient.mu)
			}
			appClients := getAppClients(client.Mac)
			for _, appClient := range appClients {
				forwardMessage(ctx, appClient.Conn, messageType, msg, appClient.mu)
			}
			break
		case Opus:
			break
		case Jpeg:
			if len(payload) < 12 {
				logger.Warningf(ctx, "Payload too short, cannot parse MAC address: %v", payload)
				return
			}
			macAddrBytes := payload[:12]
			data := payload[12:]
			macAddr := string(macAddrBytes)
			newMsg := createMessage(msgType, data)
			stackChanClient := getStackChanClient(macAddr)
			if stackChanClient != nil {
				if stackChanClient.phoneScreen {
					forwardMessage(ctx, stackChanClient.Conn, messageType, newMsg, stackChanClient.mu)
				}
			}
			break
		case ControlAvatar, ControlMotion:
			if len(payload) < 12 {
				logger.Warningf(ctx, "Payload too short, cannot parse MAC address: %v", payload)
				return
			}
			macAddrBytes := payload[:12]
			data := payload[12:]
			macAddr := string(macAddrBytes)
			newMsg := createMessage(msgType, data)
			stackChanClient := getStackChanClient(macAddr)
			if stackChanClient != nil {
				forwardMessage(ctx, stackChanClient.Conn, messageType, newMsg, stackChanClient.mu)
			} else {
				logger.Infof(ctx, "StackChan is currently offline")
			}
			break
		case TextMessage:
			if len(payload) < 12 {
				logger.Warningf(ctx, "Payload too short, cannot parse MAC address: %v", payload)
				return
			}
			macAddr := string(payload[:12])
			data := payload[12:]
			newMsg := createMessage(msgType, data)
			stackChanClient := getStackChanClient(macAddr)
			if stackChanClient != nil {
				forwardMessage(ctx, stackChanClient.Conn, messageType, newMsg, stackChanClient.mu)
			}
			appClients := getAppClients(macAddr)
			if appClients != nil {
				for _, appClient := range appClients {
					forwardMessage(ctx, appClient.Conn, messageType, newMsg, appClient.mu)
				}
			}
			break
		case RequestCall:
			// Request call
			if len(payload) < 12 {
				logger.Warningf(ctx, "Payload too short, cannot parse MAC address: %v", payload)
				return
			}
			macAddr := string(payload[:12])
			data := payload[12:]
			stackChanClient := getStackChanClient(macAddr)
			if stackChanClient != nil {
				stackChanClient.mu.Lock()
				if stackChanClient.CallAppClient == nil || stackChanClient.CallAppClient == client {
					stackChanClient.CallAppClient = client
					stackChanClient.mu.Unlock()
					newMsg := createMessage(msgType, data)
					forwardMessage(ctx, stackChanClient.Conn, messageType, newMsg, stackChanClient.mu)
				} else {
					stackChanClient.mu.Unlock()
					// Notify App that the other side is already in a call
					newMsg := createStringMessage(inCall, "The other party is currently in a call")
					forwardMessage(ctx, client.Conn, messageType, newMsg, client.mu)
				}
			}
			break
		case HangupCall:
			stackChanClientPool.Range(func(_, value any) bool {
				stackChanClient := value.(*StackChanClient)
				if stackChanClient.CallAppClient == client {
					// Found corresponding call
					stackChanClient.mu.Lock()
					stackChanClient.CallAppClient = nil
					forwardMessage(ctx, stackChanClient.Conn, messageType, msg, stackChanClient.mu)

					newList := stackChanClient.CameraSubscriptionList[:0]
					for _, sub := range stackChanClient.CameraSubscriptionList {
						if sub != client {
							newList = append(newList, sub)
						}
					}
					stackChanClient.CameraSubscriptionList = newList
					stackChanClient.mu.Unlock()
					if len(stackChanClient.CameraSubscriptionList) == 0 {
						offMsg := createMessage(OffCamera, nil)
						offType := websocket.BinaryMessage
						forwardMessage(ctx, stackChanClient.Conn, &offType, offMsg, stackChanClient.mu)
					}
					return false
				}
				return true
			})
			break
		case OnCamera:
			macAddr := string(payload)
			stackChanClient := getStackChanClient(macAddr)
			if stackChanClient != nil {
				stackChanClient.mu.Lock()
				alreadySubscribed := false
				for _, sub := range stackChanClient.CameraSubscriptionList {
					if sub == client {
						alreadySubscribed = true
						break
					}
				}
				if !alreadySubscribed {
					stackChanClient.CameraSubscriptionList = append(stackChanClient.CameraSubscriptionList, client)
					stackChanClient.mu.Unlock()
					forwardMessage(ctx, stackChanClient.Conn, messageType, msg, stackChanClient.mu)
				} else {
					stackChanClient.mu.Unlock()
				}
			}
			break
		case OffCamera:
			macAddr := string(payload)
			stackChanClient := getStackChanClient(macAddr)
			if stackChanClient != nil {
				stackChanClient.mu.Lock()
				existed := false
				newList := stackChanClient.CameraSubscriptionList[:0]
				for _, subClient := range stackChanClient.CameraSubscriptionList {
					if subClient == client {
						existed = true
					} else {
						newList = append(newList, subClient)
					}
				}
				shouldNotify := existed && len(newList) == 0
				stackChanClient.CameraSubscriptionList = newList
				stackChanClient.mu.Unlock()
				if shouldNotify {
					forwardMessage(ctx, stackChanClient.Conn, messageType, msg, stackChanClient.mu)
				}
			}
			break
		case OnPhoneScreen:
			// Show phone screen
			macAddr := string(payload)
			stackChanClient := getStackChanClient(macAddr)
			if stackChanClient != nil {
				stackChanClient.mu.Lock()
				if stackChanClient.phoneScreen == false {
					stackChanClient.phoneScreen = true
					stackChanClient.mu.Unlock()
					forwardMessage(ctx, stackChanClient.Conn, messageType, msg, stackChanClient.mu)
				} else {
					stackChanClient.mu.Unlock()
				}
			}
			break
		case OffPhoneScreen:
			// Hide phone screen
			macAddr := string(payload)
			stackChanClient := getStackChanClient(macAddr)
			if stackChanClient != nil {
				stackChanClient.mu.Lock()
				if stackChanClient.phoneScreen == true {
					stackChanClient.phoneScreen = false
					stackChanClient.mu.Unlock()
					forwardMessage(ctx, stackChanClient.Conn, messageType, msg, stackChanClient.mu)
				} else {
					stackChanClient.mu.Unlock()
				}
			}
			break
		case Dance:
			// Dance message
			stackChanClient := getStackChanClient(client.Mac)
			if stackChanClient != nil {
				forwardMessage(ctx, stackChanClient.Conn, messageType, msg, stackChanClient.mu)
			}
			break
		case GetAvatarPosture:
			stackChanClient := getStackChanClient(client.Mac)
			if stackChanClient != nil {
				forwardMessage(ctx, stackChanClient.Conn, messageType, msg, stackChanClient.mu)
			}
		default:
			logger.Infof(ctx, "Unknown binary msgType: %d", msgType)
			stackChanClient := getStackChanClient(client.Mac)
			if stackChanClient != nil {
				forwardMessage(ctx, stackChanClient.Conn, messageType, msg, stackChanClient.mu)
			}
		}
	} else if *messageType == websocket.TextMessage {
		// Directly forward other message types
		stackChanClient := getStackChanClient(client.Mac)
		if stackChanClient != nil {
			forwardMessage(ctx, stackChanClient.Conn, messageType, msg, stackChanClient.mu)
		}
	} else if *messageType == websocket.PingMessage {
		logger.Info(ctx, "Received ping message from App side")
	}
}

// forwardMessage forwards a message to the specified connection, with mutex for concurrency safety
func forwardMessage(ctx context.Context, conn *websocket.Conn, messageType *int, msg *[]byte, mu *sync.RWMutex) {
	if conn == nil {
		logger.Infof(ctx, "StackChan is currently offline")
		return
	}
	mu.Lock()
	defer mu.Unlock()
	err := conn.WriteMessage(*messageType, *msg)
	if err != nil {
		//logger.Info(ctx, "Message forwarding failed: %v", err)
	} else {
		//logger.Info(ctx, "Message sent successfully")
	}
}

// createMessage encapsulates a binary message according to custom protocol (type + length + data)
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

// createStringMessage creates a binary message with a string payload
func createStringMessage(msgType byte, data string) *[]byte {
	return createMessage(msgType, []byte(data))
}

// GetRandomStackChanDevice get Random StackChan Device list
func GetRandomStackChanDevice(userMac string, maxLength int) (list []string) {
	if maxLength <= 0 {
		return []string{}
	}
	var macs []string

	stackChanClientPool.Range(func(key, value interface{}) bool {
		mac := key.(string)
		client := value.(*StackChanClient)

		if mac == userMac {
			return true
		}

		client.mu.RLock()
		online := client.Conn != nil
		client.mu.RUnlock()

		if online {
			macs = append(macs, mac)
		}

		return true
	})

	if len(macs) == 0 {
		return []string{}
	}

	r := rand.New(rand.NewSource(time.Now().UnixNano()))
	r.Shuffle(len(macs), func(i, j int) {
		macs[i], macs[j] = macs[j], macs[i]
	})

	if len(macs) > maxLength {
		macs = macs[:maxLength]
	}

	return macs
}
