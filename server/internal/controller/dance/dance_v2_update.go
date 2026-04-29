/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

package dance

import (
	"context"
	"encoding/json"
	"stackChan/internal/dao"
	"stackChan/internal/model/do"

	"github.com/gogf/gf/v2/errors/gcode"
	"github.com/gogf/gf/v2/errors/gerror"

	"stackChan/api/dance/v2"
)

func (c *ControllerV2) Update(ctx context.Context, req *v2.UpdateReq) (res *v2.UpdateRes, err error) {
	if req.Id == 0 { // Adjust based on actual type of req.Id (int/string)
		return nil, gerror.NewCode(gcode.CodeInvalidParameter, "Dance ID cannot be empty")
	}
	updateData := do.DeviceDance{}
	if req.MusicUrl != "" {
		updateData.MusicUrl = req.MusicUrl
	}
	if req.DanceName != "" {
		updateData.DanceName = req.DanceName
	}
	if req.DanceData != nil {
		danceJSON, err := json.Marshal(req.DanceData)
		if err != nil {
			// Wrap serialization error, add business prompt
			return nil, gerror.NewCode(gcode.CodeInvalidParameter, "Dance data serialization failed: %v")
		}
		updateData.DanceData = danceJSON
	}
	_, err = dao.DeviceDance.Ctx(ctx).Where("id=?", req.Id).Data(updateData).Update()
	if err != nil {
		return nil, err
	}
	return new(v2.UpdateRes("Update successful")), nil
}
