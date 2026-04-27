/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

package admin

import (
	"context"
	"stackChan/api/admin/v1"
	"stackChan/internal/dao"
	"stackChan/internal/model"
	"stackChan/internal/model/do"

	"github.com/gogf/gf/v2/errors/gcode"
	"github.com/gogf/gf/v2/errors/gerror"
)

func (c *ControllerV1) AddApp(ctx context.Context, req *v1.AddAppReq) (res *v1.AddAppRes, err error) {
	app := do.AppStore{
		AppName:     req.AppName,
		AppIconUrl:  req.AppIconUrl,
		Description: req.Description,
		FirmwareUrl: req.FirmwareUrl,
	}
	id, err := dao.AppStore.Ctx(ctx).Data(&app).InsertAndGetId()
	if err != nil {
		return nil, gerror.WrapCode(gcode.CodeInternalError, err, "Failed to insert app")
	}
	var appInfo model.AppInfo
	err = dao.AppStore.Ctx(ctx).Where("id", id).Scan(&appInfo)
	if err != nil {
		return nil, gerror.WrapCode(gcode.CodeInternalError, err, "Failed to fetch inserted app")
	}
	res = (*v1.AddAppRes)(&appInfo)
	return res, nil
}
