/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

package device

import (
	"context"
	"stackChan/internal/dao"
	"stackChan/internal/model/do"

	"stackChan/api/device/v1"
)

func (c *ControllerV1) UpdateDeviceInfo(ctx context.Context, req *v1.UpdateDeviceInfoReq) (res *v1.UpdateDeviceInfoRes, err error) {
	doDevice := do.Device{}
	if req.Name != "" {
		doDevice.Name = req.Name
	}
	_, err = dao.Device.Ctx(ctx).Data(doDevice).WherePri(req.Mac).Update()
	if err != nil {
		return nil, err
	}
	response := v1.UpdateDeviceInfoRes("Update successful")
	return &response, nil
}
