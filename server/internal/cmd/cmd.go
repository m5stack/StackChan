/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

package cmd

import (
	"context"
	"fmt"
	"net"
	"net/http"
	"path/filepath"
	"stackChan/internal/controller/dance"
	"stackChan/internal/controller/device"
	"stackChan/internal/controller/file"
	"stackChan/internal/controller/friend"
	"stackChan/internal/controller/post"
	"stackChan/internal/web_socket"
	"time"

	"github.com/gogf/gf/v2/frame/g"
	"github.com/gogf/gf/v2/net/ghttp"
	"github.com/gogf/gf/v2/os/gcmd"
	"github.com/gogf/gf/v2/os/gfile"
	"github.com/gogf/gf/v2/os/gtimer"
)

var (
	Main = gcmd.Command{
		Name:  "main",
		Usage: "main",
		Brief: "start http server",
		Func: func(ctx context.Context, parser *gcmd.Parser) (err error) {
			PrintIPAddr()

			//Start a scheduled task to send ping messages
			gtimer.SetInterval(ctx, time.Second*5, func(ctx context.Context) {
				web_socket.StartPingTime(ctx)
			})
			//Start a timer to clean up long-lived connections that have been inactive for a long time on the app.
			gtimer.SetInterval(ctx, time.Second*15, func(ctx context.Context) {
				web_socket.CheckExpiredLinks(ctx)
			})

			s := g.Server()
			s.BindHandler("/stackChan/ws", web_socket.Handler)

			///Configuration file access
			s.Group("/file", func(group *ghttp.RouterGroup) {
				group.GET("/*filepath", func(r *ghttp.Request) {
					relativePath := r.Get("filepath").String()
					if relativePath == "" {
						r.Response.WriteHeader(http.StatusNotFound)
						r.Response.Write("File not found")
						return
					}
					filePath := filepath.Join("file", relativePath)
					if !gfile.Exists(filePath) {
						r.Response.WriteHeader(http.StatusNotFound)
						r.Response.Write("File not found")
						return
					}
					r.Response.ServeFile(filePath)
				})
			})

			s.Group("/stackChan", func(group *ghttp.RouterGroup) {
				group.Middleware(ghttp.MiddlewareHandlerResponse)
				group.Bind(device.NewV1(), friend.NewV1(), dance.NewV1(), file.NewV1(), post.NewV1())
			})
			s.Run()
			return nil
		},
	}
)

func PrintIPAddr() {
	addrs, err := net.InterfaceAddrs()
	if err == nil {
		fmt.Println("Local IP addresses detected on this machine:")
		for _, addr := range addrs {
			if ipnet, ok := addr.(*net.IPNet); ok && !ipnet.IP.IsLoopback() && ipnet.IP.To4() != nil {
				fmt.Println("  -", ipnet.IP.String())
			}
		}
	} else {
		fmt.Println("Could not detect local IP addresses:", err)
	}
	fmt.Println("Please update the StackChan and iOS client access addresses to use one of the above local IPs as needed.")

}
