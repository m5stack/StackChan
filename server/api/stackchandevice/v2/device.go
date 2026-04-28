/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

package v2

import (
	"stackChan/internal/model"

	"github.com/gogf/gf/v2/frame/g"
)

type GetDeviceUserInfoReq struct {
	g.Meta `path:"/device/user" method:"get" tags:"Device" summary:"Get device information for StackChan device"`
}

type GetDeviceUserInfoRes *model.User

type UnbindDeviceReq struct {
	g.Meta `path:"/device/unbind" method:"post" tags:"Device" summary:"Unbind device from current user for StackChan device"`
}

type UnbindDeviceRes bool
