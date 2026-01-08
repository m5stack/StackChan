/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

package v1

import "github.com/gogf/gf/v2/frame/g"

type AddReq struct {
	g.Meta    `path:"/friend" method:"post" tags:"Friend" summary:"Friend add request"`
	Mac       string `json:"mac" v:"required" description:"Mac address"`
	FriendMac string `json:"friendMac" v:"required" description:"Friend Mac address"`
}

type AddRes string
