/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

package stackchandevice

import (
	"context"
	"stackChan/internal/dao"
	"stackChan/internal/model"
	"stackChan/internal/model/entity"
	"stackChan/internal/service"

	"github.com/gogf/gf/v2/errors/gcode"
	"github.com/gogf/gf/v2/errors/gerror"
	"github.com/gogf/gf/v2/frame/g"

	"stackChan/api/stackchandevice/v2"
)

// GetDeviceUserInfo Get user information corresponding to the device
func (c *ControllerV2) GetDeviceUserInfo(ctx context.Context, req *v2.GetDeviceUserInfoReq) (res *v2.GetDeviceUserInfoRes, err error) {
	// 1. Get MAC address from context
	mac := g.RequestFromCtx(ctx).GetCtxVar(model.Mac).String()
	if mac == "" {
		return nil, gerror.NewCodef(gcode.CodeInvalidParameter, "Device MAC address is empty")
	}

	// 2. Ensure MAC record exists
	_, err = service.CreateMacIfNotExists(ctx, mac)
	if err != nil {
		return nil, gerror.WrapCode(gcode.CodeInternalError, err, "create mac record failed")
	}

	// 3. Query device information based on MAC
	var device entity.Device
	err = dao.Device.Ctx(ctx).Where("mac", mac).Scan(&device)
	if err != nil {
		return nil, gerror.WrapCodef(gcode.CodeInternalError, err, "Failed to query device information")
	}

	// 4. Device not bound to user -> return null
	if device.Uid == 0 {
		return new(v2.GetDeviceUserInfoRes(nil)), nil
	}

	// 5. Query user information based on UID
	var user model.User
	err = dao.User.Ctx(ctx).Where("uid", device.Uid).Scan(&user)
	if err != nil {
		return nil, gerror.WrapCode(gcode.CodeInternalError, err, "query user info failed")
	}

	// 6. User does not exist -> return null
	if user.Uid == 0 {
		return new(v2.GetDeviceUserInfoRes(nil)), nil
	}

	// 7. Return username normally
	return new(v2.GetDeviceUserInfoRes(&user)), nil
}
