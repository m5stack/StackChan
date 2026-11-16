/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

package service

import (
	"context"
	"errors"
	"net/http"
	"net/url"
	v2 "stackChan/api/device/v2"
	"stackChan/internal/dao"
	"stackChan/internal/model"
	"stackChan/internal/model/do"
	"stackChan/internal/model/entity"
	xiaozhiModel "stackChan/internal/model/xiaozhi"
	"stackChan/internal/xiaozhi"
	"strings"
	"time"

	"github.com/gogf/gf/v2/database/gdb"
	"github.com/gogf/gf/v2/errors/gcode"
	"github.com/gogf/gf/v2/errors/gerror"
	"github.com/gogf/gf/v2/frame/g"
	"github.com/gogf/gf/v2/os/gtime"
)

const (
	m5StackVerificationOKBody         = "OK"
	m5StackVerificationFailedMessage  = "Network query failed"
	nonStackChanMainControllerMessage = "This main controller is not from StackChan. You can only use this app with a StackChan main controller"
)

func CreateMacIfNotExists(ctx context.Context, mac string) (id int64, err error) {
	if err = validateMac(mac); err != nil {
		return 0, err
	}

	_, err = dao.Device.Ctx(ctx).Data(do.Device{
		Mac: mac,
	}).InsertIgnore()
	if err != nil {
		return 0, err
	}
	return 0, nil
}

func CreateMacIfNotExistsWithTx(ctx context.Context, tx gdb.TX, mac string) error {
	if err := validateMac(mac); err != nil {
		return err
	}

	_, err := dao.Device.Ctx(ctx).TX(tx).Data(do.Device{
		Mac: mac,
	}).InsertIgnore()
	return err
}

func validateMac(mac string) error {
	if mac == "" {
		return gerror.NewCode(gcode.CodeMissingParameter, "Device MAC address cannot be empty")
	}
	return nil
}

func GetDeviceName(ctx context.Context, mac string) (name string, err error) {
	var device entity.Device
	err = dao.Device.Ctx(ctx).Where("mac = ?", mac).Fields("name").Scan(&device)
	if err != nil {
		return "", err
	}
	return device.Name, nil
}

// ActivateAndBindDevice activates a device and binds it to the current user.
func ActivateAndBindDevice(ctx context.Context, req *v2.ActivateAndBindDeviceReq) (res *v2.ActivateAndBindDeviceRes, err error) {
	if req == nil {
		return nil, gerror.NewCode(gcode.CodeInvalidParameter, "Request cannot be empty")
	}
	if err = validateMac(req.Mac); err != nil {
		return nil, err
	}

	// Validate the user ID.
	uid := g.RequestFromCtx(ctx).GetCtxVar(model.Uid).Int64()
	if uid == 0 {
		return nil, gerror.NewCode(gcode.CodeMissingParameter, "User UID cannot be empty")
	}

	// 1. Validate this MAC belongs to an M5Stack factory device.
	if err = validateM5StackDevice(ctx, req.Mac); err != nil {
		return new(v2.ActivateAndBindDeviceRes(false)), err
	}

	// 2. Query XiaoZhi activation state; inactive devices go through activation first.
	devices, err := xiaozhi.GetDevices(nil, nil, &req.Mac, nil, nil, nil)
	if err != nil {
		return nil, gerror.WrapCode(gcode.CodeInternalError, err, "Failed to query device activation status")
	}

	agentID := findActivatedXiaoZhiAgentID(devices)
	deviceNotFoundInXiaoZhi := !hasXiaoZhiDevice(devices)
	if agentID == 0 {
		license, err := xiaozhi.GenerateLicense(req.Mac)
		if err != nil {
			return nil, gerror.WrapCode(gcode.CodeInternalError, err, "Failed to generate device license")
		}

		activationMac, err := formatXiaoZhiActivationMac(req.Mac)
		if err != nil {
			return nil, err
		}

		activateResponse, err := xiaozhi.ActivateDevice(license.SerialNumber, activationMac)
		if err != nil {
			if deviceNotFoundInXiaoZhi && errors.Is(err, xiaozhi.ErrDeviceAlreadyAdded) {
				return nil, gerror.NewCode(gcode.CodeBusinessValidationFailed, "This device has been activated for other Open Source SmartThings accounts. Please first unbind this device from your Open Source SmartThings account before attempting to rebind it.")
			}
			return nil, gerror.WrapCode(gcode.CodeInternalError, err, "Device cloud activation failed")
		}
		if !activateResponse {
			return nil, gerror.NewCode(gcode.CodeInternalError, "Device cloud activation failed")
		}

		devices, err = xiaozhi.GetDevices(nil, nil, nil, &license.SerialNumber, nil, nil)
		if err != nil {
			return nil, gerror.WrapCode(gcode.CodeInternalError, err, "Failed to confirm device activation status")
		}
		agentID = findActivatedXiaoZhiAgentID(devices)
		if agentID == 0 {
			return nil, gerror.NewCode(gcode.CodeInternalError, "Device activation confirmation failed")
		}
	}

	// 3. Reset the agent to the default template before binding, whether it was newly activated or already active.
	restoreResponse, err := RestoreDefaultAgentByAgentID(agentID, req.Mac)
	if err != nil {
		return nil, gerror.WrapCode(gcode.CodeInternalError, err, "Failed to restore default configuration")
	}
	if !restoreResponse {
		return nil, gerror.NewCode(gcode.CodeInternalError, "Failed to restore default configuration")
	}

	if err = BindDeviceToUser(ctx, uid, req.Mac); err != nil {
		return nil, gerror.WrapCode(gcode.CodeDbOperationError, err, "Device binding failed")
	}

	return new(v2.ActivateAndBindDeviceRes(true)), nil
}

func validateM5StackDevice(ctx context.Context, mac string) error {
	queryMacAddressUrl := strings.TrimSpace(g.Cfg().MustGet(ctx, "m5stack.queryMacAddressUrl").String())
	queryMacAddressToken := strings.TrimSpace(g.Cfg().MustGet(ctx, "m5stack.queryMacAddressToken").String())

	if queryMacAddressUrl == "" {
		return gerror.NewCode(gcode.CodeInternalError, "Device MAC verification URL is not configured")
	}
	if queryMacAddressToken == "" {
		return gerror.NewCode(gcode.CodeInternalError, "Device MAC verification token is not configured")
	}

	requestURL := queryMacAddressUrl + url.PathEscape(mac)
	start := time.Now()
	g.Log().Infof(ctx, "M5Stack device verification request, method=GET, url=%s, mac=%s", requestURL, mac)

	clientResp, err := g.Client().Header(g.MapStrStr{
		model.Authorization: queryMacAddressToken,
	}).Get(ctx, requestURL)
	elapsed := time.Since(start)
	if err != nil {
		g.Log().Errorf(ctx, "M5Stack device verification request failed, mac=%s, elapsed=%s, err=%+v", mac, elapsed, err)
		return newM5StackVerificationFailedError()
	}
	if clientResp == nil || clientResp.Response == nil {
		g.Log().Errorf(ctx, "M5Stack device verification no response, mac=%s, elapsed=%s", mac, elapsed)
		return newM5StackVerificationFailedError()
	}
	defer clientResp.Close()

	respBody := clientResp.ReadAllString()
	g.Log().Infof(ctx, "M5Stack device verification response, mac=%s, status=%d, elapsed=%s, response=%s", mac, clientResp.StatusCode, elapsed, respBody)
	if isM5StackVerificationServerError(clientResp.StatusCode) {
		g.Log().Errorf(ctx, "M5Stack device verification server error, mac=%s, status=%d, response=%s", mac, clientResp.StatusCode, respBody)
		return newM5StackVerificationFailedError()
	}
	if clientResp.StatusCode != http.StatusOK || respBody != m5StackVerificationOKBody {
		g.Log().Warningf(ctx, "M5Stack device verification failed, mac=%s, status=%d, response=%s", mac, clientResp.StatusCode, respBody)
		return newNonStackChanMainControllerError()
	}
	return nil
}

func isM5StackVerificationServerError(statusCode int) bool {
	return statusCode >= http.StatusInternalServerError
}

func newM5StackVerificationFailedError() error {
	return gerror.NewCode(gcode.CodeInternalError, m5StackVerificationFailedMessage)
}

func newNonStackChanMainControllerError() error {
	return gerror.NewCode(gcode.CodeBusinessValidationFailed, nonStackChanMainControllerMessage)
}

func findActivatedXiaoZhiAgentID(devices *[]xiaozhiModel.Device) int {
	if devices == nil {
		return 0
	}
	for _, device := range *devices {
		if device.AgentID != 0 {
			return device.AgentID
		}
	}
	return 0
}

func hasXiaoZhiDevice(devices *[]xiaozhiModel.Device) bool {
	return devices != nil && len(*devices) > 0
}

func formatXiaoZhiActivationMac(mac string) (string, error) {
	cleanMac := strings.ToUpper(strings.NewReplacer(":", "", "-", "").Replace(mac))
	if len(cleanMac) != 12 {
		return "", gerror.NewCode(gcode.CodeInvalidParameter, "Invalid device MAC address")
	}
	for _, char := range cleanMac {
		if (char < '0' || char > '9') && (char < 'A' || char > 'F') {
			return "", gerror.NewCode(gcode.CodeInvalidParameter, "Invalid device MAC address")
		}
	}

	parts := make([]string, 0, 6)
	for i := 0; i < len(cleanMac); i += 2 {
		parts = append(parts, cleanMac[i:i+2])
	}
	return strings.Join(parts, ":"), nil
}

func BindDeviceToUser(ctx context.Context, uid int64, mac string) error {
	return g.DB().Transaction(ctx, func(ctx context.Context, tx gdb.TX) error {
		if err := CreateMacIfNotExistsWithTx(ctx, tx, mac); err != nil {
			return err
		}

		_, err := dao.Device.Ctx(ctx).TX(tx).
			Where("mac = ?", mac).
			Data(do.Device{
				Uid:      uid,
				BindTime: gtime.Now().Format("Y-m-d H:i:s"),
			}).
			Update()
		return err
	})
}
