/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

package device

import (
	"context"
	"stackChan/internal/dao"
	"stackChan/internal/model"

	"github.com/gogf/gf/v2/errors/gcode"
	"github.com/gogf/gf/v2/errors/gerror"
	"github.com/gogf/gf/v2/frame/g"

	"stackChan/api/device/v2"
)

func (c *ControllerV2) UpdateDevice(ctx context.Context, req *v2.UpdateDeviceReq) (res *v2.UpdateDeviceRes, err error) {
	// Get current logged-in user UID
	uid := g.RequestFromCtx(ctx).GetCtxVar(model.Uid).Int64()
	if uid == 0 {
		return nil, gerror.NewCode(gcode.CodeMissingParameter, "User UID cannot be empty")
	}

	// check device exists and belongs to current user
	count, err := dao.Device.Ctx(ctx).
		Where("mac = ?", req.Mac).
		Where("uid = ?", uid).
		Count()
	if err != nil {
		return nil, gerror.WrapCode(gcode.CodeDbOperationError, err, "Failed to query device information")
	}
	if count == 0 {
		return nil, gerror.NewCode(gcode.CodeNotFound, "device not found or not belong to current user")
	}

	// build update data
	updateData := g.Map{}
	if req.Name != "" {
		updateData["name"] = req.Name
	}
	if req.Longitude != 0 {
		updateData["longitude"] = req.Longitude
	}
	if req.Latitude != 0 {
		updateData["latitude"] = req.Latitude
	}

	// no need to update
	if len(updateData) == 0 {
		return new(v2.UpdateDeviceRes(true)), nil
	}

	// update device information
	_, err = dao.Device.Ctx(ctx).
		Where("mac = ?", req.Mac).
		Where("uid = ?", uid).
		Data(updateData).
		Update()
	if err != nil {
		return nil, gerror.WrapCode(gcode.CodeDbOperationError, err, "update device failed")
	}

	return new(v2.UpdateDeviceRes(true)), nil
}
