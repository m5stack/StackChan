/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

// =================================================================================
// Code generated and maintained by GoFrame CLI tool. DO NOT EDIT.
// =================================================================================

package admin

import (
	"context"

	"stackChan/api/admin/v1"
)

type IAdminV1 interface {
	AdminLogin(ctx context.Context, req *v1.AdminLoginReq) (res *v1.AdminLoginRes, err error)
	AddApp(ctx context.Context, req *v1.AddAppReq) (res *v1.AddAppRes, err error)
	GetAppList(ctx context.Context, req *v1.GetAppListReq) (res *v1.GetAppListRes, err error)
	DeleteApp(ctx context.Context, req *v1.DeleteAppReq) (res *v1.DeleteAppRes, err error)
	UpdateApp(ctx context.Context, req *v1.UpdateAppReq) (res *v1.UpdateAppRes, err error)
}
