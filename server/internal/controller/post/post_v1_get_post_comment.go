/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

package post

import (
	"context"
	"stackChan/api/post/v1"
	"stackChan/internal/dao"
	"stackChan/internal/model"
	"stackChan/internal/model/entity"
)

func (c *ControllerV1) GetPostComment(ctx context.Context, req *v1.GetPostCommentReq) (res *v1.GetPostCommentRes, err error) {
	page := req.Page
	pageSize := req.PageSize
	if page <= 0 {
		page = 1
	}
	if pageSize <= 0 {
		pageSize = 10
	}
	offset := (page - 1) * pageSize

	var list []*model.PostComment

	db := dao.DevicePostComment.Ctx(ctx).As("dp").Where("mac = ? AND post_id = ?", req.Mac, req.PostId)

	total, err := db.Count()
	if err != nil {
		return
	}

	err = db.Order("created_at ASC").Limit(offset, pageSize).Scan(&list)
	if err != nil {
		return nil, err
	}

	for i := 0; i < len(list); i++ {
		mac := list[i].Mac
		var device entity.Device
		err = dao.Device.Ctx(ctx).Where("mac", mac).Scan(&device)
		if err != nil {
			return nil, err
		}
		list[i].Name = device.Name
	}

	res = &v1.GetPostCommentRes{
		List:  list,
		Total: total,
	}

	return res, nil
}
