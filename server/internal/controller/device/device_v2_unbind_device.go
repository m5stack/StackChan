/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

package device

import (
	"context"
	"stackChan/internal/dao"
	"stackChan/internal/model"
	"stackChan/internal/service"
	"stackChan/internal/xiaozhi"

	"github.com/gogf/gf/v2/errors/gcode"
	"github.com/gogf/gf/v2/errors/gerror"
	"github.com/gogf/gf/v2/frame/g"

	"stackChan/api/device/v2"
)

// UnbindDevice Device unbinding interface
func (c *ControllerV2) UnbindDevice(ctx context.Context, req *v2.UnbindDeviceReq) (res *v2.UnbindDeviceRes, err error) {
	_, err = service.CreateMacIfNotExists(ctx, req.Mac)
	if err != nil {
		return nil, gerror.WrapCode(gcode.CodeDbOperationError, err, "Failed to initialize device information")
	}
	uid := g.RequestFromCtx(ctx).GetCtxVar(model.Uid).Int64()
	if uid == 0 {
		return nil, gerror.NewCode(gcode.CodeMissingParameter, "User UID cannot be empty")
	}

	// 3. Validate MAC address parameter
	if req.Mac == "" {
		return nil, gerror.NewCode(gcode.CodeMissingParameter, "Device MAC address cannot be empty")
	}

	restoreResponse, err := service.RestoreDefaultAgent(req.Mac)

	if err != nil {
		return nil, err
	}

	if !restoreResponse {
		return nil, gerror.NewCode(gcode.CodeInternalError, "Failed to restore default configuration")
	}

	// xiaozhi Unbind Device
	unbindResponse, err := xiaozhi.UnbindDevice(&req.Mac)
	if err != nil {
		return nil, gerror.NewCode(gcode.CodeInternalError)
	}
	if !unbindResponse {
		g.Log().Error(ctx, "xiaozhi Unbind Device failed:")
		return nil, gerror.NewCode(gcode.CodeInternalError)
	}

	// 4. Perform unbind: set uid to 0/NULL (only the current user's own device can be unbound)
	_, err = dao.Device.Ctx(ctx).
		Where("mac = ?", req.Mac).
		Where("uid = ?", uid).
		Data("uid", nil, "bind_time", nil).
		Update()
	if err != nil {
		return nil, gerror.WrapCode(gcode.CodeDbOperationError, err, "Device unbinding failed")
	}
	// 5. Return success response (consistent with bind interface format)
	return new(v2.UnbindDeviceRes(true)), nil
}
