/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

package device

import (
	"context"
	"stackChan/api/device/v2"
)

func (c *ControllerV2) AgentRestoreDefault(ctx context.Context, req *v2.AgentRestoreDefaultReq) (res *v2.AgentRestoreDefaultRes, err error) {
	return new(v2.AgentRestoreDefaultRes(true)), err
	//if req.Mac == "" {
	//	return nil, gerror.NewCode(gcode.CodeMissingParameter, "Device MAC address cannot be empty")
	//}
	//restoreResponse, err := service.RestoreDefaultAgent(req.Mac)
	//if err != nil {
	//	return nil, err
	//}
	//if !restoreResponse {
	//	return nil, gerror.NewCode(gcode.CodeInternalError, "Failed to restore default configuration")
	//}
	//return new(v2.AgentRestoreDefaultRes(true)), nil
}
