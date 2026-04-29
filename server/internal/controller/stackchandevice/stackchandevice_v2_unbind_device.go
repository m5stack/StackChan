/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

package stackchandevice

import (
	"context"
	"stackChan/internal/dao"
	"stackChan/internal/model"
	"stackChan/internal/service"
	"stackChan/internal/xiaozhi"

	"github.com/gogf/gf/v2/errors/gcode"
	"github.com/gogf/gf/v2/errors/gerror"
	"github.com/gogf/gf/v2/frame/g"

	"stackChan/api/stackchandevice/v2"
)

// UnbindDevice Unbind device from StackChan side
func (c *ControllerV2) UnbindDevice(ctx context.Context, req *v2.UnbindDeviceReq) (res *v2.UnbindDeviceRes, err error) {
	mac := g.RequestFromCtx(ctx).GetCtxVar(model.Mac).String()
	if mac == "" {
		return nil, gerror.NewCode(gcode.CodeInvalidParameter)
	}
	_, err = service.CreateMacIfNotExists(ctx, mac)
	if err != nil {
		return nil, gerror.NewCode(gcode.CodeInternalError)
	}
	restoreResponse, err := service.RestoreDefaultAgent(mac)
	if err != nil {
		return nil, err
	}
	if !restoreResponse {
		return nil, gerror.NewCode(gcode.CodeInternalError, "restore default agent failed")
	}

	/// xiaozhi Unbind Device
	unbindResponse, err := xiaozhi.UnbindDevice(&mac)
	if err != nil {
		return nil, gerror.NewCode(gcode.CodeInternalError)
	}
	if !unbindResponse {
		g.Log().Error(ctx, "xiaozhi Unbind Device failed")
		return nil, gerror.NewCode(gcode.CodeInternalError)
	}

	/// update device table
	_, err = dao.Device.Ctx(ctx).
		Where("mac", mac).
		Data("uid", nil, "bind_time", nil).
		Update()
	if err != nil {
		return nil, gerror.NewCodef(gcode.CodeInternalError, "device unbind failed: %v", err)
	}

	return new(v2.UnbindDeviceRes(true)), nil
}
