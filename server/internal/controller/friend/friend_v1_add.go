/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

package friend

import (
	"context"
	"stackChan/internal/dao"
	"stackChan/internal/model"
	"stackChan/internal/model/entity"

	"github.com/gogf/gf/v2/errors/gcode"
	"github.com/gogf/gf/v2/errors/gerror"
	"github.com/gogf/gf/v2/frame/g"

	"stackChan/api/friend/v1"
)

func (c *ControllerV1) Add(ctx context.Context, req *v1.AddReq) (res *v1.AddRes, err error) {
	mac := g.RequestFromCtx(ctx).GetCtxVar(model.Mac).String()
	if mac == "" {
		return nil, gerror.NewCode(gcode.CodeInvalidParameter)
	}
	if mac == req.FriendMac {
		return nil, gerror.New("You cannot add yourself as a friend")
	}
	macA := mac
	macB := req.FriendMac
	var friend entity.DeviceFriend
	err = dao.DeviceFriend.Ctx(ctx).
		Where("mac_a", macA).
		Where("mac_b", macB).
		Scan(&friend)
	if err != nil {
		return nil, err
	}
	if friend.MacA == "" {
		err = dao.DeviceFriend.Ctx(ctx).
			Where("mac_a", macB).
			Where("mac_b", macA).
			Scan(&friend)
		if err != nil {
			return nil, err
		}
	}
	if friend.MacA != "" {
		res1 := v1.AddRes("Successfully added a friend")
		return &res1, nil
	}
	_, err = dao.DeviceFriend.Ctx(ctx).Data(entity.DeviceFriend{
		MacA: macA,
		MacB: macB,
	}).Insert()
	if err != nil {
		return nil, err
	}
	res2 := v1.AddRes("Successfully added a friend")
	return &res2, nil
}
