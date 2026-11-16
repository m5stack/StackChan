/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

// =================================================================================
// Code generated and maintained by GoFrame CLI tool. DO NOT EDIT.
// =================================================================================

package stackchandevice

import (
	"context"

	"stackChan/api/stackchandevice/v2"
)

type IStackchandeviceV2 interface {
	GetDeviceUserInfo(ctx context.Context, req *v2.GetDeviceUserInfoReq) (res *v2.GetDeviceUserInfoRes, err error)
	UnbindDevice(ctx context.Context, req *v2.UnbindDeviceReq) (res *v2.UnbindDeviceRes, err error)
}
