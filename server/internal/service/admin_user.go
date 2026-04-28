/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

package service

import (
	"context"
	v1 "stackChan/api/admin/v1"
	"stackChan/internal/model"
	"time"

	"github.com/gogf/gf/v2/errors/gcode"
	"github.com/gogf/gf/v2/errors/gerror"
	"github.com/gogf/gf/v2/frame/g"
	"github.com/gogf/gf/v2/os/gctx"
	"github.com/golang-jwt/jwt/v5"
)

type UserInfo struct {
	Username string `json:"username"`
	Password string `json:"password"`
}

func GetJwtSecret() string {
	var ctx = gctx.New()
	secret := g.Cfg().MustGet(ctx, "jwt.secret").String()
	return secret
}

func AdminLogin(ctx context.Context, req *v1.AdminLoginReq) (res *v1.AdminLoginRes, err error) {
	users, err := LoadUserConfig()
	if err != nil {
		return nil, err
	}
	var matched *UserInfo
	for _, user := range users {
		if user.Username == req.UserName && user.Password == req.Password {
			matched = &user
			break
		}
	}
	if matched == nil {
		return nil, gerror.NewCode(gcode.CodeNotAuthorized)
	}
	claims := jwt.MapClaims{
		model.Username: matched.Username,
		model.Exp:      time.Now().Add(24 * time.Hour).Unix(),
	}
	tokenObj := jwt.NewWithClaims(jwt.SigningMethodHS256, claims)
	jwtSecret := GetJwtSecret()
	if jwtSecret == "" {
		return nil, gerror.NewCode(gcode.CodeInternalError)
	}
	token, err := tokenObj.SignedString([]byte(jwtSecret))
	if err != nil {
		return nil, err
	}
	res = &v1.AdminLoginRes{
		Token: token,
	}
	return res, nil
}

func LoadUserConfig() ([]UserInfo, error) {
	var ctx = gctx.New()
	var users []UserInfo
	err := g.Cfg().MustGet(ctx, "admin.users").Scan(&users)
	return users, err
}
