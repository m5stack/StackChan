/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

package admin

import (
	"context"
	"stackChan/internal/service"

	"stackChan/api/admin/v1"
)

func (c *ControllerV1) AdminLogin(ctx context.Context, req *v1.AdminLoginReq) (res *v1.AdminLoginRes, err error) {
	return service.AdminLogin(ctx, req)
}
