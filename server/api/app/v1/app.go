/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

package v1

import (
	"github.com/gogf/gf/v2/frame/g"
)

type GetAppListReq struct {
	g.Meta `path:"/apps" method:"get" tags:"App" summary:"List installable apps offered by the App Center"`
}

type AppInfo struct {
	AppName     string `json:"appName"     dc:"App display name"`
	IconUrl     string `json:"iconUrl"     dc:"Icon URL"`
	Description string `json:"description" dc:"Short description"`
	FirmwareUrl string `json:"firmwareUrl" dc:"OTA firmware URL for installation"`
}

type GetAppListRes []AppInfo
