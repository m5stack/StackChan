/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

package model

import (
	"context"
	"sync"
	"time"

	"github.com/gogf/gf/v2/frame/g"
	"github.com/gorilla/websocket"
)

type WsSendMsg struct {
	MsgType int
	Data    []byte
}

type AppClient struct {
	mac      string
	conn     *websocket.Conn
	mu       sync.RWMutex
	deviceId string
	lastTime time.Time

	sendChan chan *WsSendMsg
	ctx      context.Context
	cancel   context.CancelFunc
}

type StackChanClient struct {
	mac                     string
	conn                    *websocket.Conn
	mu                      sync.RWMutex
	cameraSubscriptionList  []*AppClient
	audioSubscriptionList   []*AppClient
	callAppClient           *AppClient
	aimedTakePhotoAppClient *AppClient
	phoneScreen             bool
	lastTime                time.Time

	sendChan chan *WsSendMsg
	ctx      context.Context
	cancel   context.CancelFunc
}

// NewAppClient creates and initializes an AppClient
func NewAppClient(mac string, conn *websocket.Conn, deviceId string) *AppClient {
	ctx, cancel := context.WithCancel(context.Background())
	client := &AppClient{
		mac:      mac,
		conn:     conn,
		deviceId: deviceId,
		lastTime: time.Now(),
		sendChan: make(chan *WsSendMsg, 100),
		ctx:      ctx,
		cancel:   cancel,
	}
	client.StartWriterCoroutine()
	return client
}

// NewStackChanClient creates and initializes a StackChanClient
func NewStackChanClient(mac string, conn *websocket.Conn, cameraSubscriptionList []*AppClient, callAppClient *AppClient, phoneScreen bool) *StackChanClient {
	ctx, cancel := context.WithCancel(context.Background())
	client := &StackChanClient{
		mac:                    mac,
		conn:                   conn,
		cameraSubscriptionList: cameraSubscriptionList,
		callAppClient:          callAppClient,
		phoneScreen:            phoneScreen,
		lastTime:               time.Now(),
		sendChan:               make(chan *WsSendMsg, 100),
		ctx:                    ctx,
		cancel:                 cancel,
	}
	client.StartWriterCoroutine()
	return client
}

// StartWriterCoroutine AppClient Start message sending coroutine
func (a *AppClient) StartWriterCoroutine() {
	go func() {
		defer func() {
			if r := recover(); r != nil {
				g.Log().Errorf(context.Background(), "AppClient writer coroutine panic: %v", r)
			}
			close(a.sendChan)
		}()

		for {
			select {
			case <-a.ctx.Done():
				return
			case msg, ok := <-a.sendChan:
				if !ok { // Channel closed
					return
				}
				if msg == nil {
					continue
				}
				a.mu.RLock()
				conn := a.conn
				a.mu.RUnlock()
				if conn == nil {
					continue
				}
				if err := conn.WriteMessage(msg.MsgType, msg.Data); err != nil {
					g.Log().Errorf(context.Background(), "AppClient send message error: %v", err)
				}
			}
		}
	}()
}

// StartWriterCoroutine StackChanClient Start message sending coroutine
func (s *StackChanClient) StartWriterCoroutine() {
	go func() {
		defer func() {
			if r := recover(); r != nil {
				g.Log().Errorf(context.Background(), "StackChan writer coroutine panic: %v", r)
			}
			close(s.sendChan)
		}()
		for {
			select {
			case <-s.ctx.Done():
				return
			case msg, ok := <-s.sendChan:
				if !ok {
					return
				}
				if msg == nil {
					continue
				}
				s.mu.RLock()
				conn := s.conn
				s.mu.RUnlock()
				if conn == nil {
					continue
				}
				if err := conn.WriteMessage(msg.MsgType, msg.Data); err != nil {
					g.Log().Errorf(context.Background(), "StackChan writer coroutine send message error: %v", err)
				}
			}
		}
	}()
}

func (a *AppClient) CloseWriterCoroutine() {
	a.cancel()
}

func (s *StackChanClient) CloseWriterCoroutine() {
	s.cancel()
}

func (a *AppClient) SendChan() chan *WsSendMsg {
	return a.sendChan
}

func (s *StackChanClient) SendChan() chan *WsSendMsg {
	return s.sendChan
}

func (a *AppClient) SetMac(mac string) {
	a.mu.Lock()
	defer a.mu.Unlock()
	a.mac = mac
}

func (a *AppClient) GetMac() string {
	a.mu.RLock()
	defer a.mu.RUnlock()
	return a.mac
}

func (a *AppClient) GetConn() *websocket.Conn {
	a.mu.RLock()
	defer a.mu.RUnlock()
	return a.conn
}

func (a *AppClient) SetConn(conn *websocket.Conn) {
	a.mu.Lock()
	defer a.mu.Unlock()
	a.conn = conn
}

func (a *AppClient) SetDeviceId(deviceId string) {
	a.mu.Lock()
	defer a.mu.Unlock()
	a.deviceId = deviceId
}

func (a *AppClient) GetDeviceId() string {
	a.mu.RLock()
	defer a.mu.RUnlock()
	return a.deviceId
}

func (a *AppClient) SetLastTime(lastTime time.Time) {
	a.mu.Lock()
	defer a.mu.Unlock()
	a.lastTime = lastTime
}

func (a *AppClient) GetLastTime() time.Time {
	a.mu.RLock()
	defer a.mu.RUnlock()
	return a.lastTime
}

func (s *StackChanClient) SetMac(mac string) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.mac = mac
}

func (s *StackChanClient) GetMac() string {
	s.mu.RLock()
	defer s.mu.RUnlock()
	return s.mac
}

func (s *StackChanClient) GetConn() *websocket.Conn {
	s.mu.RLock()
	defer s.mu.RUnlock()
	return s.conn
}

func (s *StackChanClient) SetConn(conn *websocket.Conn) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.conn = conn
}

func (s *StackChanClient) SetCameraSubscriptionList(cameraSubscriptionList []*AppClient) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.cameraSubscriptionList = cameraSubscriptionList
}

func (s *StackChanClient) AppendCameraSubscriptionList(appClient *AppClient) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.cameraSubscriptionList = append(s.cameraSubscriptionList, appClient)
}

func (s *StackChanClient) GetCameraSubscriptionList() []*AppClient {
	s.mu.RLock()
	defer s.mu.RUnlock()
	out := make([]*AppClient, len(s.cameraSubscriptionList))
	copy(out, s.cameraSubscriptionList)
	return out
}

func (s *StackChanClient) SetAudioSubscriptionList(audioSubscriptionList []*AppClient) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.audioSubscriptionList = audioSubscriptionList
}

func (s *StackChanClient) GetAudioSubscriptionList() []*AppClient {
	s.mu.RLock()
	defer s.mu.RUnlock()
	out := make([]*AppClient, len(s.audioSubscriptionList))
	copy(out, s.audioSubscriptionList)
	return out
}

func (s *StackChanClient) SetCallAppClient(client *AppClient) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.callAppClient = client
}

func (s *StackChanClient) GetCallAppClient() *AppClient {
	s.mu.RLock()
	defer s.mu.RUnlock()
	return s.callAppClient
}

func (s *StackChanClient) GetAimedTakePhotoAppClient() *AppClient {
	s.mu.RLock()
	defer s.mu.RUnlock()
	return s.aimedTakePhotoAppClient
}

func (s *StackChanClient) SetAimedTakePhotoAppClient(aimedTakePhotoAppClient *AppClient) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.aimedTakePhotoAppClient = aimedTakePhotoAppClient
}

func (s *StackChanClient) GetPhoneScreen() bool {
	s.mu.RLock()
	defer s.mu.RUnlock()
	return s.phoneScreen
}

func (s *StackChanClient) SetPhoneScreen(phoneScreen bool) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.phoneScreen = phoneScreen
}

func (s *StackChanClient) GetLastTime() time.Time {
	s.mu.RLock()
	defer s.mu.RUnlock()
	return s.lastTime
}

func (s *StackChanClient) SetLastTime(lastTime time.Time) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.lastTime = lastTime
}
