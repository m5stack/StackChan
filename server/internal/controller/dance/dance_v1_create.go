/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

package dance

import (
	"context"
	"encoding/json"
	"stackChan/internal/dao"
	"stackChan/internal/model/do"

	"stackChan/api/dance/v1"

	"github.com/gogf/gf/v2/errors/gcode"
	"github.com/gogf/gf/v2/errors/gerror"
)

func (c *ControllerV1) Create(ctx context.Context, req *v1.CreateReq) (res *v1.CreateRes, err error) {
	if req.Index < 0 {
		return nil, gerror.NewCode(gcode.CodeInvalidParameter, "Index cannot be negative")
	}

	device, err := dao.Device.Ctx(ctx).Where("mac=", req.Mac).One()

	if err != nil {
		return nil, err
	}

	if device.IsEmpty() {
		_, err = dao.Device.Ctx(ctx).Data(dao.Device.Columns().Mac, req.Mac).Insert()
		if err != nil {
			return nil, err
		}
	}

	dance, err := dao.DeviceDance.Ctx(ctx).Where("mac=?", req.Mac).Where("dance_index=?", req.Index).One()
	if err != nil {
		return nil, err
	}

	danceListJSON, err := json.Marshal(req.List)
	if err != nil {
		return nil, err
	}

	if dance.IsEmpty() {
		_, err = dao.DeviceDance.Ctx(ctx).Data(do.DeviceDance{
			Mac:        req.Mac,
			DanceIndex: req.Index,
			DanceData:  danceListJSON,
		}).Insert()
		if err != nil {
			return nil, err
		}
	} else {
		_, err = dao.DeviceDance.Ctx(ctx).Where("mac=?", req.Mac).Where("dance_index=?", req.Index).Data(do.DeviceDance{
			DanceData: danceListJSON,
		}).Update()
		if err != nil {
			return nil, err
		}
	}
	response := v1.CreateRes("Dance data saved successfully")
	return &response, nil
}
