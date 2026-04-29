/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

package dance

import (
	"context"
	"stackChan/api/dance/v1"
)

func (c *ControllerV1) GetMusicList(ctx context.Context, req *v1.GetMusicListReq) (res *v1.GetMusicListRes, err error) {
	var list = make([]string, 1)
	list = append(list, "file/music/stackchan_music.mp3")
	return new(v1.GetMusicListRes(list)), nil
}
