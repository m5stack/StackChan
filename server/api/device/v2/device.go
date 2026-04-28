/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

package v2

import (
	"stackChan/internal/model"

	"github.com/gogf/gf/v2/frame/g"
)

type GetDevicesReq struct {
	g.Meta `path:"/devices" method:"get" tags:"Device" summary:"Devices Get request"`
}

type GetDevicesRes []model.DeviceInfo

type BindDeviceReq struct {
	g.Meta `path:"/device/bind" method:"post" tags:"Device" summary:"Bind device to current user"`
	Mac    string `json:"mac" v:"required" dc:"Device MAC address"`
}

type BindDeviceRes bool

type UnbindDeviceReq struct {
	g.Meta `path:"/device/unbind" method:"post" tags:"Device" summary:"Unbind device from current user"`
	Mac    string `json:"mac" v:"required" dc:"Device MAC address"`
}

type UnbindDeviceRes bool

type UpdateDeviceReq struct {
	g.Meta    `path:"/device/update" method:"put" tags:"Device" summary:"Update device name for current user's bound device"`
	Mac       string  `json:"mac" v:"required" dc:"Device MAC address"`
	Name      string  `json:"name" dc:"New device name"`
	Longitude float64 `json:"longitude" dc:"Device longitude"`
	Latitude  float64 `json:"latitude"  dc:"Device latitude"`
}

type UpdateDeviceRes bool

type AgentRestoreDefaultReq struct {
	g.Meta `path:"/device/agent/restore" method:"post" tags:"Device" summary:"Restore Agent to default template settings"`
	Mac    string `json:"mac" v:"required" dc:"Device MAC address"`
}

type AgentRestoreDefaultRes bool
