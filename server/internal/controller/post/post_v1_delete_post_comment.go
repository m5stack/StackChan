/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

package post

import (
	"context"
	"errors"
	"stackChan/internal/dao"
	"stackChan/internal/model"

	"stackChan/api/post/v1"

	"github.com/gogf/gf/v2/errors/gcode"
	"github.com/gogf/gf/v2/errors/gerror"
	"github.com/gogf/gf/v2/frame/g"
)

func (c *ControllerV1) DeletePostComment(ctx context.Context, req *v1.DeletePostCommentReq) (res *v1.DeletePostCommentRes, err error) {
	mac := g.RequestFromCtx(ctx).GetCtxVar(model.Mac).String()
	if mac == "" {
		return nil, gerror.NewCode(gcode.CodeInvalidParameter)
	}
	var postComment model.PostComment
	err = dao.DevicePostComment.Ctx(ctx).Where("id=? AND post_id=? AND mac=?", req.CommentId, req.PostId, mac).Scan(&postComment)

	if err != nil {
		return nil, err
	}

	if postComment.Id == 0 {
		return nil, errors.New("post not found")
	}

	if postComment.Mac != mac {
		return nil, errors.New("no authority to delete")
	}

	_, err = dao.DevicePostComment.
		Ctx(ctx).
		Where("id=? AND post_id=?", req.CommentId, req.PostId).
		Delete()
	if err != nil {
		return nil, err
	}

	return &v1.DeletePostCommentRes{}, nil
}
