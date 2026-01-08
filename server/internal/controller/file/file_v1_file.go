/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

package file

import (
	"context"
	"stackChan/internal/service"

	"stackChan/api/file/v1"
)

func (c *ControllerV1) File(ctx context.Context, req *v1.FileReq) (res *v1.FileRes, err error) {
	return service.AddFile(ctx, req)
}
