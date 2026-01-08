/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

package post

import (
	"context"
	"stackChan/internal/dao"
	"stackChan/internal/model/do"

	"stackChan/api/post/v1"
)

func (c *ControllerV1) CreatePostComment(ctx context.Context, req *v1.CreatePostCommentReq) (res *v1.CreatePostCommentRes, err error) {
	id, err := dao.DevicePostComment.Ctx(ctx).Data(do.DevicePostComment{
		PostId:  req.PostId,
		Mac:     req.Mac,
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
