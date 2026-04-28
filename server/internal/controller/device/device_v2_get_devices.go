/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

package device

import (
	"context"
	"stackChan/internal/dao"
	"stackChan/internal/model"

	"github.com/gogf/gf/v2/errors/gcode"
	"github.com/gogf/gf/v2/errors/gerror"
	"github.com/gogf/gf/v2/frame/g"

	"stackChan/api/device/v2"
)

func (c *ControllerV2) GetDevices(ctx context.Context, req *v2.GetDevicesReq) (res *v2.GetDevicesRes, err error) {
	uid := g.RequestFromCtx(ctx).GetCtxVar(model.Uid).Int64()
	if uid == 0 {
		return nil, gerror.NewCode(gcode.CodeMissingParameter, "user UID is required")
	}
	devices := make([]model.DeviceInfo, 0)
	err = dao.Device.Ctx(ctx).Where("uid=?", uid).Scan(&devices)
	if err != nil {
		return nil, err
	}
	return new(v2.GetDevicesRes(devices)), nil
}
