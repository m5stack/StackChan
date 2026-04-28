/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

package dance

import (
	"context"
	"encoding/json"
	"stackChan/internal/dao"
	"stackChan/internal/model"
	"stackChan/internal/model/do"

	"stackChan/api/dance/v1"

	"github.com/gogf/gf/v2/errors/gcode"
	"github.com/gogf/gf/v2/errors/gerror"
	"github.com/gogf/gf/v2/frame/g"
)

func (c *ControllerV1) Update(ctx context.Context, req *v1.UpdateReq) (res *v1.UpdateRes, err error) {
	mac := g.RequestFromCtx(ctx).GetCtxVar(model.Mac).String()
	if mac == "" {
		return nil, gerror.NewCode(gcode.CodeInvalidParameter, "MAC address cannot be empty")
	}
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
	_, err = dao.DeviceDance.Ctx(ctx).Where("mac=?", mac).Where("id=?", req.Id).Data(updateData).Update()
	if err != nil {
		return nil, err
	}
	return new(v1.UpdateRes("Update successful")), nil
}
