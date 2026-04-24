/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

package device

import (
	"context"

	"stackChan/api/device/v1"
)

// GetUserAccountInfo returns the username bound to the authenticated device.
// The firmware calls this endpoint right after boot to sync the account
// label displayed in the setup app (see firmware/main/hal/hal_account.cpp).
//
// This minimal implementation returns a default placeholder so that
// self-hosted deployments work out of the box. Production deployments are
// expected to extend this handler with a real account lookup based on the
// Authorization header token.
func (c *ControllerV1) GetUserAccountInfo(ctx context.Context, req *v1.GetUserAccountInfoReq) (res *v1.GetUserAccountInfoRes, err error) {
	return &v1.GetUserAccountInfoRes{Username: "Self-hosted User"}, nil
}
