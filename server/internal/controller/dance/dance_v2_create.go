/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

package dance

import (
	"context"
	"stackChan/internal/dao"
	"stackChan/internal/model/do"

	"github.com/gogf/gf/v2/database/gdb"
	"github.com/gogf/gf/v2/errors/gcode"
	"github.com/gogf/gf/v2/errors/gerror"
	"github.com/gogf/gf/v2/frame/g"

	"stackChan/api/dance/v2"
)

func (c *ControllerV2) Create(ctx context.Context, req *v2.CreateReq) (res *v2.CreateRes, err error) {
	mac := req.Mac
	if req.DanceName == "" {
		return nil, gerror.NewCode(gcode.CodeInvalidParameter, "Dance name cannot be empty")
	}
	if len(req.DanceData) == 0 || string(req.DanceData) == "null" {
		return nil, gerror.NewCode(gcode.CodeInvalidParameter, "Dance data cannot be empty or null")
	}
	err = g.DB().Transaction(ctx, func(ctx context.Context, tx gdb.TX) error {
		device, err := dao.Device.Ctx(ctx).TX(tx).Where("mac=?", mac).One()
		if err != nil && !gerror.HasCode(err, gcode.CodeNotFound) {
			return gerror.NewCode(gcode.CodeInternalError, "Failed to query device: %v", err.Error())
		}
		if device.IsEmpty() {
			_, err = dao.Device.Ctx(ctx).TX(tx).Data(dao.Device.Columns().Mac, mac).Insert()
			if err != nil {
				return gerror.NewCode(gcode.CodeInternalError, "Failed to create device: %v", err.Error())
			}
		}
		exist, err := dao.DeviceDance.Ctx(ctx).TX(tx).
			Where("mac=?", mac).
			Where("dance_name=?", req.DanceName).
			Exist()
		if err != nil {
			return gerror.NewCode(gcode.CodeInternalError, "Failed to check duplicate dance data: %v", err.Error())
		}
		if exist {
			return gerror.NewCode(gcode.CodeBusinessValidationFailed, "Dance data with MAC %s and name '%s' already exists", mac, req.DanceName)
		}
		_, err = dao.DeviceDance.Ctx(ctx).TX(tx).Data(do.DeviceDance{
			Mac:       mac,
			DanceData: req.DanceData,
			DanceName: req.DanceName,
			MusicUrl:  req.MusicUrl,
		}).Insert()
		if err != nil {
			return gerror.NewCode(gcode.CodeInternalError, "Failed to insert dance data: %v", err.Error())
		}

		return nil
	})
	if err != nil {
		return nil, err
	}
	return new(v2.CreateRes("Dance data saved successfully")), nil
}
