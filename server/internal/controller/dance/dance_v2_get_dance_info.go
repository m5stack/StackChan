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

	"stackChan/api/dance/v2"
)

func (c *ControllerV2) GetDanceInfo(ctx context.Context, req *v2.GetDanceInfoReq) (res *v2.GetDanceInfoRes, err error) {
	if req.Id == 0 {
		return nil, gerror.NewCode(gcode.CodeInvalidParameter, "The dance ID cannot be left blank.")
	}
	var dance model.Dance
	err = dao.DeviceDance.Ctx(ctx).Where("id=?", req.Id).Scan(&dance)
	if err != nil {
		return nil, gerror.NewCode(gcode.CodeInternalError)
	}
	return new(v2.GetDanceInfoRes(dance)), nil
}
