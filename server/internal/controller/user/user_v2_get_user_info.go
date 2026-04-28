/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

package user

import (
	"context"
	"stackChan/internal/dao"
	"stackChan/internal/model"

	"github.com/gogf/gf/v2/errors/gcode"
	"github.com/gogf/gf/v2/errors/gerror"
	"github.com/gogf/gf/v2/frame/g"

	"stackChan/api/user/v2"
)

func (c *ControllerV2) GetUserInfo(ctx context.Context, req *v2.GetUserInfoReq) (res *v2.GetUserInfoRes, err error) {
	uid := g.RequestFromCtx(ctx).GetCtxVar(model.Uid).Int64()
	if uid == 0 {
		return nil, gerror.NewCode(gcode.CodeMissingParameter, "user UID is required")
	}
	var userInfo model.User
	err = dao.User.Ctx(ctx).Where("uid=?", uid).Scan(&userInfo)
	if err != nil {
		return nil, gerror.WrapCode(gcode.CodeDbOperationError, err, "failed to query user information")
	}
	if userInfo.Uid == 0 {
		return nil, gerror.NewCode(gcode.CodeNotFound, "user does not exist")
	}
	return new(v2.GetUserInfoRes(userInfo)), nil
}
