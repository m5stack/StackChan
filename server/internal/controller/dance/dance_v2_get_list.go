/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

package dance

import (
	"context"
	"stackChan/api/dance/v2"
	"stackChan/internal/service"
)

func (c *ControllerV2) GetList(ctx context.Context, req *v2.GetListReq) (res *v2.GetListRes, err error) {
	danceList, err := service.GetOrCreateDanceList(ctx, req.Mac, "http://47.113.125.164:12800/file/music/stackchan_music.mp3")
	if err != nil {
		return nil, err
	}

	return new(v2.GetListRes(danceList)), nil
}
