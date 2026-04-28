/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

package device

import (
	"context"
	"stackChan/internal/dao"
	"stackChan/internal/model"

	"stackChan/api/device/v1"

	"github.com/gogf/gf/v2/errors/gcode"
	"github.com/gogf/gf/v2/errors/gerror"
	"github.com/gogf/gf/v2/frame/g"
)

func (c *ControllerV1) GetDeviceInfo(ctx context.Context, req *v1.GetDeviceInfoReq) (res *v1.GetDeviceInfoRes, err error) {
	mac := g.RequestFromCtx(ctx).GetCtxVar(model.Mac).String()
	if mac == "" {
		return nil, gerror.NewCode(gcode.CodeInvalidParameter)
	}
	var info model.DeviceInfo
	err = dao.Device.Ctx(ctx).WherePri(mac).Scan(&info)
	if err != nil {
		return nil, err
	}
	res = (*v1.GetDeviceInfoRes)(&info)
	return res, nil
}
