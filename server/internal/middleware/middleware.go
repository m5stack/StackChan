/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

package middleware

import (
	"stackChan/internal/model"
	"stackChan/internal/service"
	"stackChan/internal/web_socket"
	"strings"

	"github.com/gogf/gf/v2/errors/gcode"
	"github.com/gogf/gf/v2/errors/gerror"
	"github.com/gogf/gf/v2/net/ghttp"
	"github.com/golang-jwt/jwt/v5"
)

// TokenAuthMiddleware token
func TokenAuthMiddleware(r *ghttp.Request) {
	mac, err := web_socket.GetMac(r)
	if err != nil {
		r.Middleware.Next()
		return
	}
	if mac != "" {
		r.SetCtxVar(model.Mac, mac)
	}
	r.Middleware.Next()
}

func V2TokenAuthMiddleware(r *ghttp.Request) {
	if strings.HasPrefix(r.URL.Path, "/stackChan/v2/user/login") || strings.HasPrefix(r.URL.Path, "/stackChan/v2/user/registration") {
		r.Middleware.Next()
		return
	}
	tokenString := r.Header.Get("token")
	if tokenString == "" {
		r.Response.WriteJsonExit(gerror.NewCode(gcode.CodeNotAuthorized, "The token cannot be empty."))
	}
	tokenString = strings.TrimPrefix(tokenString, "Bearer ")
	tokenString = strings.TrimSpace(tokenString)
	if tokenString == "" {
		r.Response.WriteJsonExit(gerror.NewCode(gcode.CodeNotAuthorized, "Invalid token format"))
	}
	jwtSecret := service.GetJwtSecret()
	if jwtSecret == "" {
		r.Response.WriteJsonExit(gerror.NewCode(gcode.CodeInternalError, "jwt The secret has not been configured."))
	}
	token, err := jwt.Parse(tokenString, func(token *jwt.Token) (interface{}, error) {
		if _, ok := token.Method.(*jwt.SigningMethodHMAC); !ok {
			return nil, gerror.NewCodef(gcode.CodeNotAuthorized, "token signing algorithm error: %v, %v", token.Header["alg"])
		}
		return []byte(jwtSecret), nil
	})
	if err != nil || !token.Valid {
		r.Response.WriteJsonExit(gerror.NewCode(gcode.CodeNotAuthorized, "The token is invalid or has expired."))
	}
	claims, ok := token.Claims.(jwt.MapClaims)
	if !ok {
		r.Response.WriteJsonExit(gerror.NewCode(gcode.CodeNotAuthorized, "Token payload format is incorrect"))
	}
	uid, ok := claims["id"].(float64)
	if !ok {
		r.Response.WriteJsonExit(gerror.NewCode(gcode.CodeNotAuthorized, "The user ID in the token is invalid."))
	}
	r.SetCtxVar(model.Uid, int64(uid))
	r.Middleware.Next()
}

// CORS allow cross-origin
func CORS(r *ghttp.Request) {
	r.Response.CORSDefault()
	r.Middleware.Next()
}

// AdminTokenAuthMiddleware Admin token validation
func AdminTokenAuthMiddleware(r *ghttp.Request) {
	if strings.HasPrefix(r.URL.Path, "/admin/stackChan/login") {
		r.Middleware.Next()
		return
	}

	tokenString := r.Header.Get("Authorization")
	if tokenString == "" {
		r.Response.WriteJsonExit(gerror.NewCode(gcode.CodeNotAuthorized, "Token missing"))
		return
	}

	jwtSecret := service.GetJwtSecret()
	if jwtSecret == "" {
		r.Response.WriteJsonExit(gerror.NewCode(gcode.CodeInternalError, "JWT secret has not been configured."))
		return
	}
	token, err := jwt.Parse(tokenString, func(token *jwt.Token) (interface{}, error) {
		return []byte(jwtSecret), nil
	})
	if err != nil || !token.Valid {
		r.Response.WriteJsonExit(gerror.NewCode(gcode.CodeNotAuthorized, "The token is invalid."))
		return
	}
	if claims, ok := token.Claims.(jwt.MapClaims); ok && token.Valid {
		if Username, ok := claims[model.Username].(string); ok {
			if Username != "" {
				r.SetCtxVar(model.Username, Username)
				r.Middleware.Next()
				return
			}
		}
	}
	r.Response.WriteJsonExit(gerror.NewCode(gcode.CodeNotAuthorized, "The username is missing in the token."))
}
