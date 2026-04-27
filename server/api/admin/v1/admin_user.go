/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

package v1

import "github.com/gogf/gf/v2/frame/g"

type AdminLoginReq struct {
	g.Meta   `path:"/login" method:"post" tags:"Info" summary:"admin login info"`
	UserName string `json:"user_name"    description:"Admin username"`
	Password string `json:"pass_word"   description:"Admin password"`
}

type AdminLoginRes struct {
	Token string `json:"token"`
}
