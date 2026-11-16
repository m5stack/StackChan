/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

package service

import (
	"context"
	"encoding/json"
	"net/http"
	"net/url"
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
	TokenExpire                            = 365 * 24 * time.Hour
	remoteAccountServiceUnavailableMessage = "The M5Stack account service is temporarily unavailable, please try again later."
	usernameCheckFailedMessage             = "Username is already registered or unavailable, please change the username and try again."
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
	loginUrl := g.Cfg().MustGet(ctx, "m5stack.loginUrl").String()

	respBody, statusCode, err := postRemoteAccount(ctx, "login", loginUrl, g.Map{
		"username": req.Username,
		"password": req.Password,
	})
	if err != nil {
		g.Log().Errorf(ctx, "Remote login no response, username=%s", req.Username)
		return nil, err
	}
	g.Log().Debugf(ctx, "Remote login raw response: %s", respBody)

	if isRemoteAccountUnexpectedResponse(statusCode, respBody) {
		g.Log().Errorf(ctx, "Remote login unexpected response: status=%d, raw response: %s", statusCode, respBody)
		return nil, newRemoteAccountUnavailableError()
	}

	if strings.Contains(respBody, "[[error:") {
		g.Log().Errorf(ctx, "Remote login failed: %s", respBody)
		return nil, gerror.NewCode(gcode.CodeBusinessValidationFailed, respBody)
	}

	remoteLoginResp := &model.RemoteLoginResp{}
	if err = json.Unmarshal([]byte(respBody), remoteLoginResp); err != nil {
		g.Log().Errorf(ctx, "Login response parsing failed: %+v, raw response: %s", err, respBody)
		return nil, newRemoteAccountUnavailableError()
	}
	if remoteLoginResp.Status.Code != "ok" {
		errMsg := remoteLoginResp.Status.Message
		if errMsg == "" {
			g.Log().Errorf(ctx, "Remote login failed with empty message: status=%d, raw response: %s", statusCode, respBody)
			return nil, newRemoteAccountUnavailableError()
		}
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
	if err = callRemoteUsernameQuery(ctx, req.UserName); err != nil {
		return nil, err
	}

	remoteResp, err := callRemoteRegister(ctx, req)
	if err != nil {
		return nil, err
	}
	return new(v2.RegistrationRes(remoteResp)), nil
}

// callRemoteUsernameQuery checks username availability before registration.
func callRemoteUsernameQuery(ctx context.Context, username string) error {
	queryUsernameUrl := g.Cfg().MustGet(ctx, "m5stack.queryUsernameUrl").String() + url.PathEscape(username)
	registrationToken := g.Cfg().MustGet(ctx, "m5stack.registrationToken").String()

	respBody, statusCode, err := getRemoteAccount(ctx, "query username", queryUsernameUrl, g.MapStrStr{
		"Authorization": registrationToken,
	})
	if err != nil {
		return err
	}
	g.Log().Debugf(ctx, "Remote query username raw response: %s", respBody)

	if err = validateRemoteUsernameQueryResponse(username, statusCode, respBody); err != nil {
		g.Log().Errorf(ctx, "Remote query username failed: username=%s, status=%d, raw response: %s", username, statusCode, respBody)
		return err
	}
	return nil
}

// callRemoteRegister Call remote registration interface
func callRemoteRegister(ctx context.Context, req *v2.RegistrationReq) (res *model.RegistrationResponse, err error) {
	resp := &model.RemoteRegisterResp{}
	g.Log().Infof(ctx, "Remote registration request parameters: username=%s, email=%s", req.UserName, req.Email)

	RegistrationToken := g.Cfg().MustGet(ctx, "m5stack.registrationToken").String()
	RegistrationUrl := g.Cfg().MustGet(ctx, "m5stack.registrationUrl").String()

	respBody, statusCode, err := postRemoteAccount(ctx, "registration", RegistrationUrl, g.Map{
		"username": req.UserName,
		"email":    req.Email,
		"password": req.Password,
	}, g.MapStrStr{
		"Authorization": RegistrationToken,
	})
	if err != nil {
		return nil, err
	}
	g.Log().Debugf(ctx, "Remote registration raw response: %s", respBody)

	if isRemoteAccountUnexpectedResponse(statusCode, respBody) {
		g.Log().Errorf(ctx, "Remote registration unexpected response: status=%d, raw response: %s", statusCode, respBody)
		return nil, newRemoteAccountUnavailableError()
	}

	if strings.Contains(respBody, "[[error:") {
		g.Log().Errorf(ctx, "Remote registration failed: %s", respBody)
		return nil, gerror.NewCode(gcode.CodeBusinessValidationFailed, respBody)
	}

	if err = json.Unmarshal([]byte(respBody), resp); err != nil {
		g.Log().Errorf(ctx, "Registration response parsing failed: %+v, raw response: %s", err, respBody)
		return nil, newRemoteAccountUnavailableError()
	}

	if resp.Status.Code != "ok" {
		if resp.Status.Message == "" {
			g.Log().Errorf(ctx, "Remote registration business failed with empty message: code=%s, raw response: %s", resp.Status.Code, respBody)
			return nil, newRemoteAccountUnavailableError()
		}
		g.Log().Errorf(ctx, "Remote registration business failed: code=%s, message=%s", resp.Status.Code, resp.Status.Message)
		return nil, gerror.NewCodef(gcode.CodeBusinessValidationFailed, resp.Status.Message)
	}
	return &resp.RegistrationResponse, nil
}

func getRemoteAccount(ctx context.Context, operation string, remoteURL string, headers ...g.MapStrStr) (respBody string, statusCode int, err error) {
	client := g.Client()
	if len(headers) > 0 {
		client = client.Header(headers[0])
	}

	clientResp, err := client.Get(ctx, remoteURL)
	if err != nil {
		g.Log().Errorf(ctx, "Remote %s request failed: %+v", operation, err)
		return "", 0, newRemoteAccountUnavailableError()
	}
	if clientResp == nil || clientResp.Response == nil {
		g.Log().Errorf(ctx, "Remote %s no response", operation)
		return "", 0, newRemoteAccountUnavailableError()
	}
	defer clientResp.Close()

	return clientResp.ReadAllString(), clientResp.StatusCode, nil
}

func postRemoteAccount(ctx context.Context, operation string, url string, data g.Map, headers ...g.MapStrStr) (respBody string, statusCode int, err error) {
	client := g.Client()
	if len(headers) > 0 {
		client = client.Header(headers[0])
	}

	clientResp, err := client.Post(ctx, url, data)
	if err != nil {
		g.Log().Errorf(ctx, "Remote %s request failed: %+v", operation, err)
		return "", 0, newRemoteAccountUnavailableError()
	}
	if clientResp == nil || clientResp.Response == nil {
		g.Log().Errorf(ctx, "Remote %s no response", operation)
		return "", 0, newRemoteAccountUnavailableError()
	}
	defer clientResp.Close()

	return clientResp.ReadAllString(), clientResp.StatusCode, nil
}

func validateRemoteUsernameQueryResponse(username string, statusCode int, respBody string) error {
	if isRemoteAccountUnexpectedResponse(statusCode, respBody) {
		return newRemoteAccountUnavailableError()
	}

	body := strings.TrimSpace(respBody)
	if body == "" {
		return newRemoteAccountUnavailableError()
	}

	if isRemoteUsernameNotFoundResponse(statusCode, body) {
		return nil
	}

	if strings.Contains(body, "[[error:") {
		return newUsernameCheckFailedError("")
	}

	if remoteUsernameQueryIndicatesDuplicate(username, json.RawMessage(body)) {
		return newUsernameCheckFailedError("")
	}

	var resp struct {
		Status struct {
			Code    string `json:"code"`
			Message string `json:"message"`
		} `json:"status"`
		Response json.RawMessage `json:"response"`
	}
	if err := json.Unmarshal([]byte(body), &resp); err != nil {
		return newRemoteAccountUnavailableError()
	}

	if resp.Status.Code != "" && !strings.EqualFold(resp.Status.Code, "ok") {
		if isRemoteUsernameNotFoundResponse(statusCode, resp.Status.Code+" "+resp.Status.Message+" "+body) {
			return nil
		}
		return newUsernameCheckFailedError(resp.Status.Message)
	}

	if remoteUsernameQueryIndicatesDuplicate(username, resp.Response) {
		return newUsernameCheckFailedError("")
	}
	return nil
}

func isRemoteAccountUnexpectedResponse(statusCode int, respBody string) bool {
	if statusCode >= http.StatusInternalServerError {
		return true
	}

	body := strings.ToLower(strings.TrimSpace(respBody))
	return strings.HasPrefix(body, "<!doctype html") ||
		strings.HasPrefix(body, "<html") ||
		(strings.Contains(body, "<html") && strings.Contains(body, "</html>"))
}

func isRemoteUsernameNotFoundResponse(statusCode int, message string) bool {
	if statusCode == http.StatusNotFound {
		return true
	}

	message = strings.ToLower(message)
	return strings.Contains(message, "not found") ||
		strings.Contains(message, "not-found") ||
		strings.Contains(message, "no user") ||
		strings.Contains(message, "no-user") ||
		strings.Contains(message, "user-not-found")
}

func remoteUsernameQueryIndicatesDuplicate(username string, response json.RawMessage) bool {
	if len(response) == 0 || string(response) == "null" {
		return false
	}

	var responseMap map[string]interface{}
	if err := json.Unmarshal(response, &responseMap); err != nil {
		return false
	}

	if hasTrueBoolValue(responseMap, "exists", "duplicate", "duplicated") {
		return true
	}
	if hasFalseBoolValue(responseMap, "available", "isAvailable") {
		return true
	}
	if stringValueEquals(responseMap, username, "username", "userName", "userslug") {
		return true
	}
	return hasNonZeroNumericValue(responseMap, "uid", "id")
}

func hasTrueBoolValue(data map[string]interface{}, keys ...string) bool {
	for _, key := range keys {
		value, ok := data[key]
		if !ok {
			continue
		}
		if boolValue, ok := value.(bool); ok && boolValue {
			return true
		}
	}
	return false
}

func hasFalseBoolValue(data map[string]interface{}, keys ...string) bool {
	for _, key := range keys {
		value, ok := data[key]
		if !ok {
			continue
		}
		if boolValue, ok := value.(bool); ok && !boolValue {
			return true
		}
	}
	return false
}

func stringValueEquals(data map[string]interface{}, want string, keys ...string) bool {
	for _, key := range keys {
		value, ok := data[key].(string)
		if ok && strings.EqualFold(value, want) {
			return true
		}
	}
	return false
}

func hasNonZeroNumericValue(data map[string]interface{}, keys ...string) bool {
	for _, key := range keys {
		value, ok := data[key].(float64)
		if ok && value > 0 {
			return true
		}
	}
	return false
}

func newRemoteAccountUnavailableError() error {
	return gerror.NewCode(gcode.CodeInternalError, remoteAccountServiceUnavailableMessage)
}

func newUsernameCheckFailedError(remoteMessage string) error {
	message := strings.TrimSpace(remoteMessage)
	if message == "" {
		message = usernameCheckFailedMessage
	} else {
		message += " Please change the username and try again."
	}
	return gerror.NewCode(gcode.CodeBusinessValidationFailed, message)
}
