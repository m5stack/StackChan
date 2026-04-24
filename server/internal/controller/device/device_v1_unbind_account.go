/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

package device

import (
	"context"

	"stackChan/api/device/v1"
)

// UnbindAccount removes the binding between the authenticated device and
// its user account. The firmware calls this endpoint when the user triggers
// "Unbind" from the setup app (see firmware/main/hal/hal_account.cpp).
//
// This minimal implementation is a no-op so the firmware flow completes
// successfully. Production deployments are expected to delete the real
// account binding identified by the Authorization header token.
func (c *ControllerV1) UnbindAccount(ctx context.Context, req *v1.UnbindAccountReq) (res *v1.UnbindAccountRes, err error) {
	return &v1.UnbindAccountRes{}, nil
}
