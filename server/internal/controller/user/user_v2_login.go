/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

package user

import (
	"context"
	"stackChan/internal/service"

	"stackChan/api/user/v2"
)

func (c *ControllerV2) Login(ctx context.Context, req *v2.LoginReq) (res *v2.LoginRes, err error) {
	return service.Login(ctx, req)
}
