/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

package xiaozhi

import (
	"os"
	"strings"
	"testing"
	"time"

	"stackChan/internal/model"
	xiaozhiModel "stackChan/internal/model/xiaozhi"

	"github.com/gogf/gf/v2/frame/g"
)

const (
	runDeleteDeviceTestEnv      = "STACKCHAN_RUN_XIAOZHI_DELETE_DEVICE_TEST"
	deleteDeviceTestMacEnv      = "STACKCHAN_XIAOZHI_DELETE_MAC"
	deleteDeviceTestAuthCodeEnv = "STACKCHAN_XIAOZHI_DELETE_AUTH_CODE"
)

func TestDeleteDeviceFromXiaoZhiByMacAndAuthCode(t *testing.T) {
	if os.Getenv(runDeleteDeviceTestEnv) != "1" {
		t.Skipf("set %s=1 to really delete a XiaoZhi device", runDeleteDeviceTestEnv)
	}

	requireXiaoZhiDeleteDeviceTestConfig(t)

	mac := strings.TrimSpace(os.Getenv(deleteDeviceTestMacEnv))
	authCode := strings.TrimSpace(os.Getenv(deleteDeviceTestAuthCodeEnv))
	if mac == "" {
		t.Fatalf("%s is empty", deleteDeviceTestMacEnv)
	}
	if authCode == "" {
		t.Fatalf("%s is empty, expected XiaoZhi serial_number/auth code", deleteDeviceTestAuthCodeEnv)
	}

	formattedMac := formatMac(&mac)
	if formattedMac == nil {
		t.Fatalf("invalid mac address: %s", mac)
	}

	page := 1
	pageSize := 10
	devices, err := GetDevices(&page, &pageSize, formattedMac, &authCode, nil, nil)
	if err != nil {
		t.Fatalf("query XiaoZhi device failed, mac=%s, auth_code=%s, reason=%v", *formattedMac, authCode, err)
	}
	if devices == nil || len(*devices) == 0 {
		t.Fatalf("XiaoZhi device not found, no deletion performed, mac=%s, auth_code=%s", *formattedMac, authCode)
	}
	if len(*devices) > 1 {
		t.Fatalf("matched multiple XiaoZhi devices, no deletion performed, mac=%s, auth_code=%s, count=%d", *formattedMac, authCode, len(*devices))
	}

	device := (*devices)[0]
	t.Logf(
		"matched XiaoZhi device, device_id=%d, agent_id=%d, product_id=%d, mac=%s, serial_number=%s",
		device.DeviceID,
		device.AgentID,
		device.ProductID,
		device.MacAddress,
		device.SerialNumber,
	)

	deleted, err := deleteXiaoZhiDeviceByID(device.DeviceID)
	if err != nil {
		t.Fatalf("delete XiaoZhi device failed, device_id=%d, mac=%s, auth_code=%s, reason=%v", device.DeviceID, *formattedMac, authCode, err)
	}
	if !deleted {
		t.Fatalf("delete XiaoZhi device returned false, device_id=%d, mac=%s, auth_code=%s", device.DeviceID, *formattedMac, authCode)
	}

	assertXiaoZhiDeviceDeleted(t, formattedMac, authCode)
	t.Logf("delete XiaoZhi device succeeded, device_id=%d, mac=%s, auth_code=%s", device.DeviceID, *formattedMac, authCode)
}

func requireXiaoZhiDeleteDeviceTestConfig(t *testing.T) {
	t.Helper()

	if strings.TrimSpace(g.Cfg().MustGet(ctx, "xiaozhi.secret_key").String()) == "" {
		t.Fatal("xiaozhi.secret_key is empty")
	}
}

func deleteXiaoZhiDeviceByID(deviceID int) (bool, error) {
	var resp xiaozhiModel.XiaoZhiResponse[model.Empty]
	err := doRequest("POST", deviceUnbind, g.Map{
		"device_id": deviceID,
	}, &resp)
	if err != nil {
		return false, err
	}
	if !resp.Success {
		if resp.Message == "device not found" {
			return true, nil
		}
		return false, newXiaoZhiFailureError("delete XiaoZhi device", resp.Message)
	}
	return true, nil
}

func assertXiaoZhiDeviceDeleted(t *testing.T, formattedMac *string, authCode string) {
	t.Helper()

	page := 1
	pageSize := 10
	var lastErr error
	for attempt := 1; attempt <= 5; attempt++ {
		devices, err := GetDevices(&page, &pageSize, formattedMac, &authCode, nil, nil)
		if err != nil {
			lastErr = err
		} else if devices == nil || len(*devices) == 0 {
			return
		}

		time.Sleep(time.Second)
	}
	if lastErr != nil {
		t.Fatalf("confirm XiaoZhi device deletion failed, mac=%s, auth_code=%s, reason=%v", *formattedMac, authCode, lastErr)
	}
	t.Fatalf("XiaoZhi device still exists after deletion, mac=%s, auth_code=%s", *formattedMac, authCode)
}
