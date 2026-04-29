/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

package dance

import (
	"context"
	"stackChan/internal/dao"
	"stackChan/internal/model"
	"stackChan/internal/model/do"

	"github.com/gogf/gf/v2/errors/gcode"
	"github.com/gogf/gf/v2/errors/gerror"
	"github.com/gogf/gf/v2/frame/g"

	"stackChan/api/dance/v1"
)

func (c *ControllerV1) GetList(ctx context.Context, req *v1.GetListReq) (res *v1.GetListRes, err error) {
	mac := g.RequestFromCtx(ctx).GetCtxVar(model.Mac).String()
	if mac == "" {
		return nil, gerror.NewCode(gcode.CodeInvalidParameter)
	}

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
			Mac:       mac,
			MusicUrl:  "file/music/stackchan_music.mp3",
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

	return new(v1.GetListRes(danceList)), nil
}
