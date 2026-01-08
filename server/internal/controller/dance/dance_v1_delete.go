/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

package dance

import (
	"context"
	"stackChan/internal/dao"

	"stackChan/api/dance/v1"
)

func (c *ControllerV1) Delete(ctx context.Context, req *v1.DeleteReq) (res *v1.DeleteRes, err error) {
	_, err = dao.DeviceDance.Ctx(ctx).Where("mac=", req.Mac).Where("dance_index=", req.Index).Delete()
	return
}
