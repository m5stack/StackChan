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
	"stackChan/internal/model/entity"
	"strconv"

	"github.com/gogf/gf/v2/errors/gcode"
	"github.com/gogf/gf/v2/errors/gerror"

	"stackChan/api/dance/v1"
)

func (c *ControllerV1) GetList(ctx context.Context, req *v1.GetListReq) (res *v1.GetListRes, err error) {
	danceMap := make(map[string][]model.DanceData)
	var list []entity.DeviceDance
	err = dao.DeviceDance.Ctx(ctx).Where(do.DeviceDance{
		Mac: req.Mac,
	}).Scan(&list)
	if err != nil {
		return nil, err
	}
	if len(list) > 0 {
		deviceDance := list[0]
		var danceList []model.DanceData
		err = json.Unmarshal([]byte(deviceDance.DanceData), &danceList)
		if err != nil {
			return nil, gerror.WrapCode(gcode.CodeInvalidParameter, err)
		}
		key := strconv.Itoa(deviceDance.DanceIndex)
		danceMap[key] = danceList
	}
	response := v1.GetListRes(danceMap)
	return &response, nil
}
