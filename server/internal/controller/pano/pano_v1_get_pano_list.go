/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

package pano

import (
	"context"
	"stackChan/internal/dao"
	"stackChan/internal/model"

	"github.com/gogf/gf/v2/errors/gcode"
	"github.com/gogf/gf/v2/errors/gerror"
	"github.com/gogf/gf/v2/frame/g"

	"stackChan/api/pano/v1"
)

func (c *ControllerV1) GetPanoList(ctx context.Context, req *v1.GetPanoListReq) (res *v1.GetPanoListRes, err error) {
	mac := g.RequestFromCtx(ctx).GetCtxVar(model.Mac).String()
	if mac == "" {
		return nil, gerror.NewCode(gcode.CodeInvalidParameter)
	}

	var list []model.Pano

	err = dao.DevicePano.Ctx(ctx).Where("mac = ?", mac).Scan(&list)

	if err != nil {
		return nil, gerror.NewCode(gcode.CodeInvalidParameter)
	}

	if list == nil {
		list = make([]model.Pano, 0)
	}

	response := v1.GetPanoListRes(list)

	return &response, nil
}
