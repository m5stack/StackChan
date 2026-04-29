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

	"github.com/gogf/gf/v2/errors/gcode"
	"github.com/gogf/gf/v2/errors/gerror"
	"github.com/gogf/gf/v2/frame/g"

	"stackChan/api/post/v1"
)

func (c *ControllerV1) CreatePost(ctx context.Context, req *v1.CreatePostReq) (res *v1.CreatePostRes, err error) {
	mac := g.RequestFromCtx(ctx).GetCtxVar(model.Mac).String()
	if mac == "" {
		return nil, gerror.NewCode(gcode.CodeInvalidParameter)
	}
	device, err := dao.Device.Ctx(ctx).Where("mac", mac).One()
	if err != nil {
		return nil, err
	}
	if device == nil {
		return nil, gerror.NewCode(gcode.CodeInvalidRequest, "The device does not exist or the Mac address is incorrect")
	}
	insertId, err := dao.DevicePost.Ctx(ctx).Data(do.DevicePost{
		Mac:          mac,
		ContentText:  req.ContentText,
		ContentImage: req.ContentImage,
	}).InsertAndGetId()
	if err != nil {
		return nil, err
	}
	res = &v1.CreatePostRes{
		Id: insertId,
	}
	return res, nil
}
