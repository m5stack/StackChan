/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

// =================================================================================
// Code generated and maintained by GoFrame CLI tool. DO NOT EDIT.
// =================================================================================

package pano

import (
	"context"

	"stackChan/api/pano/v1"
)

type IPanoV1 interface {
	AddPano(ctx context.Context, req *v1.AddPanoReq) (res *v1.AddPanoRes, err error)
	GetPanoList(ctx context.Context, req *v1.GetPanoListReq) (res *v1.GetPanoListRes, err error)
}
