/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

package device

import (
	"context"
	"stackChan/internal/model"
	"stackChan/internal/service"

	"github.com/gogf/gf/v2/errors/gcode"
	"github.com/gogf/gf/v2/errors/gerror"
	"github.com/gogf/gf/v2/frame/g"

	"stackChan/api/device/v2"
)

// BindDevice Device binding interface
func (c *ControllerV2) BindDevice(ctx context.Context, req *v2.BindDeviceReq) (res *v2.BindDeviceRes, err error) {
	uid := g.RequestFromCtx(ctx).GetCtxVar(model.Uid).Int64()
	if uid == 0 {
		return nil, gerror.NewCode(gcode.CodeMissingParameter, "User UID cannot be empty")
	}
	if req.Mac == "" {
		return nil, gerror.NewCode(gcode.CodeMissingParameter, "Device MAC address cannot be empty")
	}

	if err = service.BindDeviceToUser(ctx, uid, req.Mac); err != nil {
		return nil, gerror.WrapCode(gcode.CodeDbOperationError, err, "Device binding failed")
	}
	return new(v2.BindDeviceRes(true)), nil
}
