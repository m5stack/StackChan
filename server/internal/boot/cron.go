/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

package boot

import (
	"context"
	"stackChan/internal/web_socket"
	"sync/atomic"
	"time"

	"github.com/gogf/gf/v2/frame/g"
)

func InitCron() {
	startPingTimer()
	startCleanTimer()
}

var (
	pingTimerStarted  atomic.Bool
	cleanTimerStarted atomic.Bool
)

// startPingTimer starts the heartbeat timer, with panic recovery and restart logic.
func startPingTimer() {
	if !pingTimerStarted.CompareAndSwap(false, true) {
		return
	}
	ctx, cancel := context.WithCancel(context.Background())
	_ = cancel // for future use
	go func() {
		ticker := time.NewTicker(5 * time.Second)
		defer ticker.Stop()
		g.Log().Info(ctx, "The heartbeat sending timer has been activated")
		for {
			select {
			case <-ctx.Done():
				pingTimerStarted.Store(false)
				return
			case <-ticker.C:
				func() {
					defer func() {
						if err := recover(); err != nil {
							g.Log().Errorf(ctx, "Heartbeat sending task crash: %v, the timer is about to be restarted", err)
							pingTimerStarted.Store(false)
							go func() {
								time.Sleep(time.Second)
								startPingTimer()
							}()
						}
					}()
					web_socket.StartPingTime(ctx)
				}()
			}
		}
	}()
}

// startCleanTimer starts the connection cleaning timer, with panic recovery and restart logic.
func startCleanTimer() {
	if !cleanTimerStarted.CompareAndSwap(false, true) {
		return
	}
	ctx, cancel := context.WithCancel(context.Background())
	_ = cancel // for future use
	go func() {
		ticker := time.NewTicker(15 * time.Second)
		defer ticker.Stop()
		g.Log().Info(ctx, "The connection cleaning timer has been started")
		for {
			select {
			case <-ctx.Done():
				cleanTimerStarted.Store(false)
				return
			case <-ticker.C:
				func() {
					defer func() {
						if err := recover(); err != nil {
							g.Log().Errorf(ctx, "Connection cleanup task crash: %v, about to restart the timer", err)
							cleanTimerStarted.Store(false)
							go func() {
								time.Sleep(time.Second)
								startCleanTimer()
							}()
						}
					}()
					web_socket.CheckExpiredLinks(ctx)
				}()
			}
		}
	}()
}
