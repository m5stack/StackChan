/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

package app

import (
	"context"

	"stackChan/api/app/v1"
)

type IAppV1 interface {
	GetAppList(ctx context.Context, req *v1.GetAppListReq) (res *v1.GetAppListRes, err error)
}
