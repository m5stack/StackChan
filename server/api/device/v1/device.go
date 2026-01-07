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
	Mac    string `json:"mac" v:"required" description:"Mac address"`
}

type GetDeviceInfoRes model.DeviceInfo

type UpdateDeviceInfoReq struct {
	g.Meta `path:"/device/info" method:"put" tags:"Device" summary:"Device Info Put request"`
	Mac    string `json:"mac" v:"required" description:"Mac address"`
	Name   string `json:"name" description:"Device name"`
}

type UpdateDeviceInfoRes string
