/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

package v2

import (
	"stackChan/internal/model"

	"github.com/gogf/gf/v2/frame/g"
)

type LoginReq struct {
	g.Meta   `path:"user/login" method:"post" tags:"Info" summary:"user login info"`
	Username string `json:"username" v:"required" description:"Account or email"`
	Password string `json:"password" v:"required" description:"Password"`
}

type LoginRes struct {
	Token string `json:"token"`
}

type GetUserInfoReq struct {
	g.Meta `path:"user" method:"get" tags:"Info" summary:"user get info"`
}

type GetUserInfoRes model.User

type RegistrationReq struct {
	g.Meta   `path:"user/registration" method:"post" tags:"Info" summary:"user registration"`
	UserName string `json:"username" v:"required" description:"Username"`
	Email    string `json:"email" v:"required" description:"Email address"`
	Password string `json:"password" v:"required" description:"Password"`
}

type RegistrationRes *model.RegistrationResponse
