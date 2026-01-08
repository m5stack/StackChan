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

	"stackChan/api/dance/v1"
)

func (c *ControllerV1) Update(ctx context.Context, req *v1.UpdateReq) (res *v1.UpdateRes, err error) {
	response := v1.UpdateRes("")
	danceJSON, err := json.Marshal(req.Data)
	if err != nil {
		return nil, err
	}
	_, err = dao.DeviceDance.Ctx(ctx).Where("mac=?", req.Mac).Where("dance_index=?", req.Index).Data(do.DeviceDance{
		DanceData: danceJSON,
	}).Update()
	if err != nil {
		return nil, err
	}
	response = "Update successful"
	return &response, nil
}
