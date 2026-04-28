/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

package v1

import "github.com/gogf/gf/v2/frame/g"

type GetXiaoZhiTokenReq struct {
	g.Meta `path:"/xiaozhi/token" method:"get" tags:"XiaoZhi" summary:"XiaoZhi token"`
}

type GetXiaoZhiTokenRes string

type RefreshTokenReq struct {
	g.Meta `path:"/xiaozhi/token/refresh" method:"get" tags:"XiaoZhi" summary:"XiaoZhi token refresh"`
}

type RefreshTokenRes string

type GetXiaoZhiGenerateLicenseTokenReq struct {
	g.Meta `path:"/xiaozhi/generateLicenseToken" method:"get" tags:"XiaoZhi" summary:"XiaoZhi generateLicenseToken"`
}

type GetXiaoZhiGenerateLicenseTokenRes string
