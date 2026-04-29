/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

package dance

import (
	"context"
	"stackChan/internal/dao"

	"stackChan/api/dance/v2"
)

func (c *ControllerV2) Delete(ctx context.Context, req *v2.DeleteReq) (res *v2.DeleteRes, err error) {
	_, err = dao.DeviceDance.Ctx(ctx).Where("id=", req.Id).Delete()
	return
}
