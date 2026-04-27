/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

package dance

import (
	"context"
	"stackChan/internal/dao"
	"stackChan/internal/model"

	"github.com/gogf/gf/v2/errors/gcode"
	"github.com/gogf/gf/v2/errors/gerror"
	"github.com/gogf/gf/v2/frame/g"

	"stackChan/api/dance/v1"
)

func (c *ControllerV1) GetDanceInfo(ctx context.Context, req *v1.GetDanceInfoReq) (res *v1.GetDanceInfoRes, err error) {
	mac := g.RequestFromCtx(ctx).GetCtxVar(model.Mac).String()
	if mac == "" {
		return nil, gerror.NewCode(gcode.CodeInvalidParameter)
	}
	if req.Id == 0 {
		return nil, gerror.NewCode(gcode.CodeInvalidParameter, "The dance ID cannot be left blank.")
	}
	var dance model.Dance
	err = dao.DeviceDance.Ctx(ctx).Where("mac=?", mac).Where("id=?", req.Id).Scan(&dance)
	if err != nil {
		return nil, gerror.NewCode(gcode.CodeInternalError)
	}
	return new(v1.GetDanceInfoRes(dance)), nil
}
