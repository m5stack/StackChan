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

func (c *ControllerV1) GetAppList(ctx context.Context, req *v1.GetAppListReq) (res *v1.GetAppListRes, err error) {
	apps, err := service.GetAppList(ctx)
	if err != nil {
		return nil, err
	}
	response := v1.GetAppListRes(apps)
	return &response, nil
}
