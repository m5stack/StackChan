/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

package post

import (
	"context"
	"stackChan/internal/dao"
	"stackChan/internal/model"
	"stackChan/internal/model/entity"

	"stackChan/api/post/v1"
)

func (c *ControllerV1) GetPost(ctx context.Context, req *v1.GetPostReq) (res *v1.GetPostRes, err error) {
	page := req.Page
	pageSize := req.PageSize
	if page <= 0 {
		page = 1
	}
	if pageSize <= 0 {
		pageSize = 10
	}

	db := dao.DevicePost.Ctx(ctx).As("dp").
		LeftJoin("device d", "dp.mac = d.mac")

	_, err = db.Count("dp.id")
	if err != nil {
		return nil, err
	}

	var list []model.Post
	err = db.Fields(
		"dp.id",
		"dp.mac",
		"d.name",
		"dp.content_text",
		"dp.content_image",
		"dp.created_at",
	).Order("dp.created_at DESC").Limit((page-1)*pageSize, pageSize).Scan(&list)
	if err != nil {
		return nil, err
	}

	for i := 0; i < len(list); i++ {
		var comments []*model.PostComment
		err = dao.DevicePostComment.Ctx(ctx).Where("post_id", list[i].Id).Order("created_at ASC").Scan(&comments)
		if err != nil {
			return nil, err
		}

		for j := 0; j < len(comments); j++ {
			mac := comments[j].Mac
			var device entity.Device
			err = dao.Device.Ctx(ctx).Where("mac", mac).Scan(&device)
			if err != nil {
				return nil, err
			}
			comments[j].Name = device.Name
		}

		list[i].PostCommentList = comments
	}
	res = (*v1.GetPostRes)(&list)
	return res, nil
}
