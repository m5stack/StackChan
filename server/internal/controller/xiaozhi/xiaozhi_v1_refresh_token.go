/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

package xiaozhi

import (
	"context"
	"stackChan/internal/xiaozhi"

	"stackChan/api/xiaozhi/v1"
)

func (c *ControllerV1) RefreshToken(ctx context.Context, req *v1.RefreshTokenReq) (res *v1.RefreshTokenRes, err error) {
	token, err := xiaozhi.GetNewToken()
	if err != nil {
		return nil, err
	}
	return new(v1.RefreshTokenRes(token)), nil
}
