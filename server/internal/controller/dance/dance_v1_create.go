/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

package dance

import (
	"context"
	"stackChan/internal/dao"
	"stackChan/internal/model"
	"stackChan/internal/model/do"

	"stackChan/api/dance/v1"

	"github.com/gogf/gf/v2/database/gdb"
	"github.com/gogf/gf/v2/errors/gcode"
	"github.com/gogf/gf/v2/errors/gerror"
	"github.com/gogf/gf/v2/frame/g"
)

func (c *ControllerV1) Create(ctx context.Context, req *v1.CreateReq) (res *v1.CreateRes, err error) {
	// 1. Get and validate MAC address (business required parameter)
	mac := g.RequestFromCtx(ctx).GetCtxVar(model.Mac).String()
	if mac == "" {
		return nil, gerror.NewCode(gcode.CodeInvalidParameter, "MAC address cannot be empty")
	}
	// 2. Auto validate using struct v tag (DanceName required), manual secondary validation as fallback
	if req.DanceName == "" {
		return nil, gerror.NewCode(gcode.CodeInvalidParameter, "Dance name cannot be empty")
	}
	// 3. Validate dance data not empty (RawMessage need to check if empty/only null)
	if len(req.DanceData) == 0 || string(req.DanceData) == "null" {
		return nil, gerror.NewCode(gcode.CodeInvalidParameter, "Dance data cannot be empty or null")
	}
	// 4. Use transaction to ensure data consistency
	err = g.DB().Transaction(ctx, func(ctx context.Context, tx gdb.TX) error {
		// 4.1 Query device, create if not exists
		device, err := dao.Device.Ctx(ctx).TX(tx).Where("mac=?", mac).One()
		if err != nil && !gerror.HasCode(err, gcode.CodeNotFound) {
			return gerror.NewCode(gcode.CodeInternalError, "Failed to query device: %v", err.Error())
		}

		// Create device if not exists
		if device.IsEmpty() {
			_, err = dao.Device.Ctx(ctx).TX(tx).Data(dao.Device.Columns().Mac, mac).Insert()
			if err != nil {
				return gerror.NewCode(gcode.CodeInternalError, "Failed to create device: %v", err.Error())
			}
		}

		// 4.2 Check if dance data with same MAC+DanceName exists (avoid duplicates)
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

		// 4.3 Insert dance data (use RawMessage directly, no need for secondary serialization)
		_, err = dao.DeviceDance.Ctx(ctx).TX(tx).Data(do.DeviceDance{
			Mac:       mac,
			DanceData: req.DanceData, // Use RawMessage directly, avoid duplicate marshal
			DanceName: req.DanceName,
			MusicUrl:  req.MusicUrl, // Add background music URL field
		}).Insert()
		if err != nil {
			return gerror.NewCode(gcode.CodeInternalError, "Failed to insert dance data: %v", err.Error())
		}

		return nil
	})
	if err != nil {
		return nil, err
	}
	response := v1.CreateRes("Dance data saved successfully")
	return &response, nil
}
