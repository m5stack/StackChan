/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

package admin

import (
	"context"

	"github.com/gogf/gf/v2/errors/gcode"
	"github.com/gogf/gf/v2/errors/gerror"

	"stackChan/api/admin/v1"
	"stackChan/internal/dao"

	"github.com/gogf/gf/v2/frame/g"
)

func (c *ControllerV1) DeleteApp(ctx context.Context, req *v1.DeleteAppReq) (res *v1.DeleteAppRes, err error) {
	_, err = dao.AppStore.Ctx(ctx).Where("id", req.Id).Data(g.Map{"is_deleted": 1}).Update()
	if err != nil {
		return nil, gerror.WrapCode(gcode.CodeInternalError, err, "Failed to delete app")
	}

	res = &v1.DeleteAppRes{}
	return res, nil
}
