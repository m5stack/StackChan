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

func (c *ControllerV2) Registration(ctx context.Context, req *v2.RegistrationReq) (res *v2.RegistrationRes, err error) {
	return service.Registration(ctx, req)
}
