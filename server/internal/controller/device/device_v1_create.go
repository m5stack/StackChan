/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

package device

import (
	"context"
	"stackChan/api/device/v1"
	"stackChan/internal/dao"
	"stackChan/internal/model"
	"stackChan/internal/model/do"

	"github.com/gogf/gf/v2/errors/gcode"
	"github.com/gogf/gf/v2/errors/gerror"
	"github.com/gogf/gf/v2/frame/g"
)

func (c *ControllerV1) Create(ctx context.Context, req *v1.CreateReq) (res *v1.CreateRes, err error) {
	mac := g.RequestFromCtx(ctx).GetCtxVar(model.Mac).String()
	if mac == "" {
		return nil, gerror.NewCode(gcode.CodeInvalidParameter)
	}
	insertId, err := dao.Device.Ctx(ctx).Data(do.Device{
		Mac:  mac,
		Name: req.Name,
	}).InsertAndGetId()
	if err != nil {
		return nil, err
	}
	res = &v1.CreateRes{
		Id: insertId,
	}
	return
}
