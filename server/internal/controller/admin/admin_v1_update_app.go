/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

package admin

import (
	"context"
	"stackChan/internal/model"

	"stackChan/api/admin/v1"
	"stackChan/internal/dao"

	"github.com/gogf/gf/v2/errors/gcode"
	"github.com/gogf/gf/v2/errors/gerror"
	"github.com/gogf/gf/v2/frame/g"
)

func (c *ControllerV1) UpdateApp(ctx context.Context, req *v1.UpdateAppReq) (res *v1.UpdateAppRes, err error) {
	updateData := g.Map{}
	if req.AppName != "" {
		updateData["app_name"] = req.AppName
	}
	if req.AppIconUrl != "" {
		updateData["app_icon_url"] = req.AppIconUrl
	}
	if req.Description != "" {
		updateData["description"] = req.Description
	}
	if req.FirmwareUrl != "" {
		updateData["firmware_url"] = req.FirmwareUrl
	}

	if len(updateData) > 0 {
		_, err = dao.AppStore.Ctx(ctx).WherePri(req.Id).Data(updateData).Update()
		if err != nil {
			return nil, gerror.WrapCode(gcode.CodeInternalError, err, "Failed to update app")
		}
	}

	var appInfo model.AppInfo
	err = dao.AppStore.Ctx(ctx).WherePri(req.Id).Scan(&appInfo)
	if err != nil {
		return nil, gerror.WrapCode(gcode.CodeInternalError, err, "Failed to fetch updated app")
	}

	res = (*v1.UpdateAppRes)(&appInfo)
	return res, nil
}
