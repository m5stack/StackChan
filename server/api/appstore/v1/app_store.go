/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

package v1

import (
	"stackChan/internal/model"

	"github.com/gogf/gf/v2/frame/g"
)

type GetAppListReq struct {
	g.Meta `path:"/apps" method:"get" tags:"App" summary:"App List Get"`
}

type GetAppListRes []model.AppInfo
