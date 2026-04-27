/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

package pano

import (
	"context"
	"stackChan/internal/dao"
	"stackChan/internal/model"
	"stackChan/internal/model/entity"

	"github.com/gogf/gf/v2/errors/gcode"
	"github.com/gogf/gf/v2/errors/gerror"
	"github.com/gogf/gf/v2/frame/g"

	"stackChan/api/pano/v1"
)

func (c *ControllerV1) AddPano(ctx context.Context, req *v1.AddPanoReq) (res *v1.AddPanoRes, err error) {
	mac := g.RequestFromCtx(ctx).GetCtxVar(model.Mac).String()
	if mac == "" {
		return nil, gerror.NewCode(gcode.CodeInvalidParameter)
	}

	if req.Url == "" {
		return nil, gerror.NewCode(gcode.CodeInvalidParameter)
	}

	Id, err := dao.DevicePano.Ctx(ctx).Data(entity.DevicePano{
		Mac:     mac,
		PanoUrl: req.Url,
	}).InsertAndGetId()

	if err != nil {
		return nil, gerror.NewCode(gcode.CodeInvalidParameter)
	}

	res = &v1.AddPanoRes{
		Id: Id,
	}

	return res, nil
}
