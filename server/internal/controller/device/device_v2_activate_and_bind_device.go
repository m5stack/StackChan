/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

package device

import (
	"context"
	"stackChan/internal/service"

	"stackChan/api/device/v2"
)

func (c *ControllerV2) ActivateAndBindDevice(ctx context.Context, req *v2.ActivateAndBindDeviceReq) (res *v2.ActivateAndBindDeviceRes, err error) {
	return service.ActivateAndBindDevice(ctx, req)
}
