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

func (c *ControllerV1) GetXiaoZhiToken(ctx context.Context, req *v1.GetXiaoZhiTokenReq) (res *v1.GetXiaoZhiTokenRes, err error) {
	token, err := xiaozhi.GetToken()
	if err != nil {
		return nil, err
	}
	return new(v1.GetXiaoZhiTokenRes(token)), nil
}
