/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

package v1

import (
	"stackChan/internal/model"

	"github.com/gogf/gf/v2/frame/g"
)

type AddAppReq struct {
	g.Meta      `path:"/app/add" method:"post" tags:"App" summary:"App add request"`
	AppName     string `json:"appName"     orm:"app_name"     v:"required" d:"" description:"App name (required)"`
	AppIconUrl  string `json:"appIconUrl"  orm:"app_icon_url" d:"" description:"App icon URL (optional)"`
	Description string `json:"description" orm:"description"  d:"" description:"App description (optional)"`
	FirmwareUrl string `json:"firmwareUrl" orm:"firmware_url" d:"" description:"Firmware / installation package download URL (optional)"`
}

type AddAppRes model.AppInfo

type GetAppListReq struct {
	g.Meta `path:"/apps" method:"get" tags:"App" summary:"App List Get"`
}

type GetAppListRes []model.AppInfo

type DeleteAppReq struct {
	g.Meta `path:"/app/delete" method:"delete" tags:"App" summary:"App delete"`
	Id     int64 `json:"id"          orm:"id"           description:"App ID"` // App ID
}

type DeleteAppRes struct{}

type UpdateAppReq struct {
	g.Meta      `path:"/app/update" method:"put" tags:"App" summary:"App put"`
	Id          int64  `json:"id"          orm:"id"           v:"required" description:"App ID (required)"`
	AppName     string `json:"appName"     orm:"app_name"     d:"" description:"App name (optional)"`
	AppIconUrl  string `json:"appIconUrl"  orm:"app_icon_url" d:"" description:"App icon URL (optional)"`
	Description string `json:"description" orm:"description"  d:"" description:"App description (optional)"`
	FirmwareUrl string `json:"firmwareUrl" orm:"firmware_url" d:"" description:"Firmware / installation package download URL (optional)"`
}

type UpdateAppRes model.AppInfo
