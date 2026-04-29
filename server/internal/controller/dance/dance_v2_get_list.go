/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

package dance

import (
	"context"
	"stackChan/api/dance/v2"
	"stackChan/internal/dao"
	"stackChan/internal/model"
	"stackChan/internal/model/do"
)

func (c *ControllerV2) GetList(ctx context.Context, req *v2.GetListReq) (res *v2.GetListRes, err error) {
	mac := req.Mac
	var danceList []model.Dance
	err = dao.DeviceDance.Ctx(ctx).Where(do.DeviceDance{
		Mac: mac,
	}).Scan(&danceList)
	if err != nil {
		return nil, err
	}

	// Core modification: insert default data only when query result is empty
	if len(danceList) == 0 {
		// Insert single default dance data
		defaultDance := do.DeviceDance{
			DanceName: "StackChan Dance",
			Mac:       mac,
			MusicUrl:  "http://47.113.125.164:12800/file/music/stackchan_music.mp3",
			DanceData: model.DefaultDanceData,
		}
		_, err = dao.DeviceDance.Ctx(ctx).Data(defaultDance).Insert()
		if err != nil {
			return nil, err
		}

		// Re-query list (one data exists now)
		err = dao.DeviceDance.Ctx(ctx).Where(do.DeviceDance{
			Mac: mac,
		}).Scan(&danceList)
		if err != nil {
			return nil, err
		}
	}

	return new(v2.GetListRes(danceList)), nil
}
