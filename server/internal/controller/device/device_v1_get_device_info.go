/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

package device

import (
	"context"
	"stackChan/internal/dao"
	"stackChan/internal/model"

	"stackChan/api/device/v1"
)

func (c *ControllerV1) GetDeviceInfo(ctx context.Context, req *v1.GetDeviceInfoReq) (res *v1.GetDeviceInfoRes, err error) {
	if req.Mac == "" {
		return (*v1.GetDeviceInfoRes)(&model.DeviceInfo{}), nil
	}
	var info model.DeviceInfo
	err = dao.Device.Ctx(ctx).WherePri(req.Mac).Scan(&info)
	if err != nil {
		return nil, err
	}
	res = (*v1.GetDeviceInfoRes)(&info)
	return res, nil
}
