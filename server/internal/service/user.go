/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

package service

import (
	"context"
	v2 "stackChan/api/user/v2"
	"stackChan/internal/dao"
	"stackChan/internal/model"
	"stackChan/internal/model/entity"
	"strings"
	"time"

	"github.com/gogf/gf/v2/errors/gcode"
	"github.com/gogf/gf/v2/errors/gerror"
	"github.com/gogf/gf/v2/frame/g"
	"github.com/gogf/gf/v2/util/guid"
	"github.com/golang-jwt/jwt/v5"
)

const (
	TokenExpire = 365 * 24 * time.Hour
)

// Login User login
func Login(ctx context.Context, req *v2.LoginReq) (res *v2.LoginRes, err error) {

	if req.Username == "" || req.Password == "" {
		return nil, gerror.NewCode(gcode.CodeMissingParameter, "Username / Password cannot be left blank.")
	}

	remoteResp, err := callRemoteLogin(ctx, req)
	if err != nil {
		return nil, err
	}
	if remoteResp == nil {
		return nil, gerror.NewCode(gcode.CodeInvalidParameter, "invalid parameter")
	}
	if err = saveUserToLocal(ctx, remoteResp); err != nil {
		return nil, err
	}
	token, err := generateToken(ctx, remoteResp.Response.Uid)
	if err != nil {
		return nil, err
	}
	return &v2.LoginRes{
		Token: token,
	}, nil
}

// callRemoteLogin Call remote login interface
func callRemoteLogin(ctx context.Context, req *v2.LoginReq) (*model.RemoteLoginResp, error) {
	remoteLoginResp := &model.RemoteLoginResp{}

	loginUrl := g.Cfg().MustGet(ctx, "m5stack.loginUrl").String()

	clientResp := g.Client().PostVar(ctx, loginUrl, g.Map{
		"username": req.Username,
		"password": req.Password,
	})
	if clientResp == nil {
		g.Log().Errorf(ctx, "Remote login no response, username=%s", req.Username)
		return nil, gerror.NewCode(gcode.CodeInternalError, "remote service unavailable")
	}
	respBody := clientResp.String()
	g.Log().Debugf(ctx, "Remote login raw response: %s", respBody)
	if strings.Contains(respBody, "[[error:") {
		g.Log().Errorf(ctx, "Remote login failed: %s", respBody)
		return nil, gerror.NewCode(gcode.CodeBusinessValidationFailed, respBody)
	}
	err := clientResp.Scan(&remoteLoginResp)
	if err != nil {
		g.Log().Errorf(ctx, "Login response parsing failed: %+v, raw response: %s", err, respBody)
		return nil, gerror.WrapCode(gcode.CodeInternalError, err, respBody)
	}
	if remoteLoginResp.Status.Code != "ok" {
		errMsg := remoteLoginResp.Status.Message
		g.Log().Errorf(ctx, "Remote login failed: %s", errMsg)
		return nil, gerror.NewCode(gcode.CodeBusinessValidationFailed, remoteLoginResp.Status.Message, errMsg)
	}
	return remoteLoginResp, nil
}

// saveUserToLocal Save user to local database
func saveUserToLocal(ctx context.Context, resp *model.RemoteLoginResp) error {
	data := entity.User{
		Uid:            resp.Response.Uid,
		Username:       resp.Response.Username,
		Userslug:       resp.Response.Userslug,
		DisplayName:    resp.Response.Displayname,
		IconText:       resp.Response.IconText,
		IconBgColor:    resp.Response.IconBgColor,
		EmailConfirmed: resp.Response.EmailConfirmed,
		JoinDate:       resp.Response.Joindate,
		LastOnline:     resp.Response.Lastonline,
		UserStatus:     resp.Response.Status,
	}
	_, err := dao.User.Ctx(ctx).Save(data)
	if err != nil {
		return gerror.WrapCode(gcode.CodeDbOperationError, err, "Failed to write user to local database")
	}
	return nil
}

// generateToken Generate JWT token, includes user UID, issuer, audience, issued time, expiration time
func generateToken(ctx context.Context, uid int64) (string, error) {
	now := time.Now()

	Issuer := g.Cfg().MustGet(ctx, "m5stack.issuer").String()
	Audience := g.Cfg().MustGet(ctx, "m5stack.audience").String()

	claims := jwt.MapClaims{
		"jti": guid.S(),                    // Unique token ID (for revocation/blacklisting)
		"id":  uid,                         // User UID
		"iss": Issuer,                      // Issuer (for verification and anti-forgery)
		"aud": Audience,                    // Audience (to limit scope of use)
		"iat": now.Unix(),                  // Issued at time
		"exp": now.Add(TokenExpire).Unix(), // Expiration time
	}
	tokenObj := jwt.NewWithClaims(jwt.SigningMethodHS256, claims)
	jwtSecret := GetJwtSecret()
	if jwtSecret == "" || len(jwtSecret) < 16 {
		return "", gerror.NewCode(gcode.CodeInternalError, "JWT secret is empty or too weak")
	}
	token, err := tokenObj.SignedString([]byte(jwtSecret))
	if err != nil {
		return "", gerror.WrapCode(gcode.CodeInternalError, err, "Failed to generate token")
	}
	return token, nil
}

// Registration User registration
func Registration(ctx context.Context, req *v2.RegistrationReq) (res *v2.RegistrationRes, err error) {
	if req.UserName == "" || req.Password == "" || req.Email == "" {
		return nil, gerror.NewCode(gcode.CodeMissingParameter, "Username/Email/Password cannot be empty")
	}
	remoteResp, err := callRemoteRegister(ctx, req)
	if err != nil {
		return nil, err
	}
	responseData := v2.RegistrationRes(remoteResp)
	return &responseData, nil
}

// callRemoteRegister Call remote registration interface
func callRemoteRegister(ctx context.Context, req *v2.RegistrationReq) (res *model.RegistrationResponse, err error) {
	resp := &model.RemoteRegisterResp{}
	g.Log().Infof(ctx, "Remote registration request parameters: username=%s, email=%s", req.UserName, req.Email)

	RegistrationToken := g.Cfg().MustGet(ctx, "m5stack.registrationToken").String()
	RegistrationUrl := g.Cfg().MustGet(ctx, "m5stack.registrationUrl").String()

	clientResp := g.Client().
		SetHeader("Authorization", RegistrationToken).
		PostVar(ctx, RegistrationUrl, g.Map{
			"username": req.UserName,
			"email":    req.Email,
			"password": req.Password,
		})

	if clientResp == nil {
		return nil, gerror.NewCode(gcode.CodeInternalError, "remote service unavailable")
	}

	respBody := clientResp.String()
	g.Log().Debugf(ctx, "Remote registration raw response: %s", respBody)

	if strings.Contains(respBody, "[[error:") {
		g.Log().Errorf(ctx, "Remote registration failed: %s", respBody)
		return nil, gerror.NewCode(gcode.CodeBusinessValidationFailed, respBody)
	}

	err = clientResp.Scan(&resp)
	if err != nil {
		g.Log().Errorf(ctx, "Registration response parsing failed: %+v, raw response: %s", err, respBody)
		return nil, gerror.WrapCode(gcode.CodeInternalError, err, respBody)
	}

	if resp.Status.Code != "ok" {
		g.Log().Errorf(ctx, "Remote registration business failed: code=%s, message=%s", resp.Status.Code, resp.Status.Message)
		return nil, gerror.NewCodef(gcode.CodeBusinessValidationFailed, resp.Status.Message)
	}
	return &resp.RegistrationResponse, nil
}
