/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

package post

import (
	"context"
	"stackChan/api/post/v1"
	"stackChan/internal/dao"
)

func (c *ControllerV1) DeletePost(ctx context.Context, req *v1.DeletePostReq) (res *v1.DeletePostRes, err error) {
	_, err = dao.DevicePost.Ctx(ctx).WherePri(req.Id).Delete()
	if err != nil {
		return nil, err
	}
	response := v1.DeletePostRes("Deletion successful")
	return &response, nil
}
