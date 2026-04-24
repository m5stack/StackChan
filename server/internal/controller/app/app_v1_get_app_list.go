/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

package app

import (
	"context"

	"stackChan/api/app/v1"
)

// GetAppList returns the list of apps offered by the App Center. The
// firmware consumes this list in firmware/main/hal/hal_app_center.cpp to
// populate the Mooncake AppCenter UI and to trigger OTA downloads.
//
// This minimal implementation returns an empty list so self-hosted
// deployments start with a clean slate. Production deployments are
// expected to replace the body with a DB-backed catalogue.
func (c *ControllerV1) GetAppList(ctx context.Context, req *v1.GetAppListReq) (res *v1.GetAppListRes, err error) {
	empty := v1.GetAppListRes{}
	return &empty, nil
}
