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

func (c *ControllerV1) Update(ctx context.Context, req *v1.UpdateReq) (res *v1.UpdateRes, err error) {
	_, err = dao.Device.Ctx(ctx).Data(do.Device{
		Name: req.Name,
	}).WherePri(req.Mac).Update()
	return
}
