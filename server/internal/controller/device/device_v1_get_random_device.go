/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

package device

import (
	"context"
	"stackChan/api/device/v1"
	"stackChan/internal/dao"
	"stackChan/internal/model"
	"stackChan/internal/model/entity"
	"stackChan/internal/web_socket"

	"github.com/gogf/gf/v2/errors/gcode"
	"github.com/gogf/gf/v2/errors/gerror"
	"github.com/gogf/gf/v2/frame/g"
)

func (c *ControllerV1) GetRandomDevice(ctx context.Context, req *v1.GetRandomDeviceReq) (res *v1.GetRandomDeviceRes, err error) {
	mac := g.RequestFromCtx(ctx).GetCtxVar(model.Mac).String()
	if mac == "" {
		return nil, gerror.NewCode(gcode.CodeInvalidParameter)
	}

	pageSize := req.PageSize
	if pageSize <= 0 {
		pageSize = 6
	}

	// Obtain the list of online StackChan mac addresses (excluding the current user) from the websocket layer.
	macList := web_socket.GetRandomStackChanDevice(mac, pageSize)

	if len(macList) == 0 {
		res = (*v1.GetRandomDeviceRes)(&[]entity.Device{})
		return res, nil
	}

	// Query device information based on the Mac list
	list := make([]entity.Device, 0, len(macList))
	err = dao.Device.
		Ctx(ctx).
		WhereIn("mac", macList).
		Scan(&list)
	if err != nil {
		return nil, err
	}
	res = (*v1.GetRandomDeviceRes)(&list)
	return res, nil
}
