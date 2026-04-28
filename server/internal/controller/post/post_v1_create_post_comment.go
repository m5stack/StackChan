/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

package post

import (
	"context"
	"stackChan/internal/dao"
	"stackChan/internal/model"
	"stackChan/internal/model/do"

	"stackChan/api/post/v1"

	"github.com/gogf/gf/v2/errors/gcode"
	"github.com/gogf/gf/v2/errors/gerror"
	"github.com/gogf/gf/v2/frame/g"
)

func (c *ControllerV1) CreatePostComment(ctx context.Context, req *v1.CreatePostCommentReq) (res *v1.CreatePostCommentRes, err error) {
	mac := g.RequestFromCtx(ctx).GetCtxVar(model.Mac).String()
	if mac == "" {
		return nil, gerror.NewCode(gcode.CodeInvalidParameter)
	}
	id, err := dao.DevicePostComment.Ctx(ctx).Data(do.DevicePostComment{
		PostId:  req.PostId,
		Mac:     mac,
		Content: req.Content,
	}).InsertAndGetId()
	if err != nil {
		return nil, err
	}
	res = &v1.CreatePostCommentRes{
		Id: id,
	}
	return res, err
}
