/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

// =================================================================================
// Code generated and maintained by GoFrame CLI tool. DO NOT EDIT.
// =================================================================================

package friend

import (
	"context"

	"stackChan/api/friend/v1"
)

type IFriendV1 interface {
	Add(ctx context.Context, req *v1.AddReq) (res *v1.AddRes, err error)
}
