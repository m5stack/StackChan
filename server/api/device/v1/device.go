/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

package v1

import (
	"stackChan/internal/model"
	"stackChan/internal/model/entity"

	"github.com/gogf/gf/v2/frame/g"
)

type CreateReq struct {
	g.Meta `path:"/device" method:"post" tags:"Device" summary:"Device create request"`
	Mac    string `json:"mac" v:"required" description:"Mac address"`
	Name   string `json:"name,omitempty" description:"Device name"`
}

type CreateRes struct {
	Id int64 `json:"id" dc:"Device id"`
}

type UpdateReq struct {
	g.Meta `path:"/device" method:"put" tags:"Device" summary:"Device update request"`
	Mac    string `json:"mac" v:"required" description:"Mac address"`
	Name   string `json:"name" description:"Device name"`
}

type UpdateRes struct{}

type GetRandomDeviceReq struct {
	g.Meta `path:"/device/randomList" method:"get" tags:"Device" summary:"Device get Random"`
	Mac    string `json:"mac" v:"required" description:"Mac address"`
}

type GetRandomDeviceRes []entity.Device

type GetDeviceInfoReq struct {
	g.Meta `path:"/device/info" method:"get" tags:"Device" summary:"Device Info Get request"`
	Mac    string `json:"mac" description:"Mac address. Optional: when empty, returns an empty device info placeholder so authenticated firmware calls that do not carry a mac still receive a well-formed response."`
}

type GetDeviceInfoRes model.DeviceInfo

type UpdateDeviceInfoReq struct {
	g.Meta `path:"/device/info" method:"put" tags:"Device" summary:"Device Info Put request"`
	Mac    string `json:"mac" v:"required" description:"Mac address"`
	Name   string `json:"name" description:"Device name"`
}

type UpdateDeviceInfoRes string

type GetUserAccountInfoReq struct {
	g.Meta `path:"/device/user" method:"get" tags:"Device" summary:"Get the user account bound to the authenticated device"`
}

type GetUserAccountInfoRes struct {
	Username string `json:"username" dc:"Username bound to the device"`
}

type UnbindAccountReq struct {
	g.Meta `path:"/device/unbind" method:"post" tags:"Device" summary:"Unbind the authenticated device from its user account"`
}

type UnbindAccountRes struct{}
