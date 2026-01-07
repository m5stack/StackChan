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
)

func (c *ControllerV1) DeletePostComment(ctx context.Context, req *v1.DeletePostCommentReq) (res *v1.DeletePostCommentRes, err error) {
	var postComment model.PostComment
	err = dao.DevicePostComment.Ctx(ctx).Where("id=?", req.Id).Scan(&postComment)

	if err != nil {
		return nil, err
	}

	if postComment.Id == 0 {
		return nil, errors.New("post not found")
	}

	if postComment.Mac != req.Mac {
		return nil, errors.New("no authority to delete")
	}

	_, err = dao.DevicePostComment.
		Ctx(ctx).
		Where("id = ? AND mac = ?", req.Id, req.Mac).
		Delete()
	if err != nil {
		return nil, err
	}

	return &v1.DeletePostCommentRes{}, nil
}
