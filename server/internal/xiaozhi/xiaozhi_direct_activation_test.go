/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

package xiaozhi

import (
	"fmt"
	"net/http"
	"net/url"
	"os"
	"strings"
	"testing"
	"time"

	"stackChan/internal/model"
	xiaozhiModel "stackChan/internal/model/xiaozhi"

	"github.com/gogf/gf/v2/encoding/gjson"
	"github.com/gogf/gf/v2/frame/g"
	"github.com/gogf/gf/v2/net/gclient"
)

const (
	// Placeholder MAC; override it with directActivationTestMacEnv before running.
	defaultDirectActivationTestMac = "00:11:22:33:44:55"
	directActivationTestMacEnv     = "STACKCHAN_DIRECT_XIAOZHI_ACTIVATION_TEST_MAC"
	// Upstream XiaoZhi API message; it must match the remote response verbatim.
	directActivationBoundOtherSerialNumberError = "该设备已绑定其他序列号"
)

func TestDirectActivateDeviceThenUnbind(t *testing.T) {
	if os.Getenv("STACKCHAN_RUN_DIRECT_XIAOZHI_ACTIVATION_TEST") != "1" {
		t.Skip("set STACKCHAN_RUN_DIRECT_XIAOZHI_ACTIVATION_TEST=1 to call the real XiaoZhi activation API")
	}

	tokenString := directActivationTestGetToken(t)
	mac := directActivationTestMac()
	formattedMac := formatMac(&mac)
	if formattedMac == nil {
		t.Fatalf("invalid MAC address: %s", mac)
	}
	t.Logf("test MAC: %s (XiaoZhi format: %s)", mac, *formattedMac)
	t.Logf("to test another device, set %s at runtime", directActivationTestMacEnv)

	licenseToken := strings.TrimSpace(g.Cfg().MustGet(ctx, "xiaozhi.generate_license_token").String())
	if licenseToken == "" {
		t.Fatal("xiaozhi.generate_license_token is empty; cannot generate a device license")
	}

	licenseResp, licenseRaw, licenseStatus := directActivationTestRequest[xiaozhiModel.GenerateLicense](
		t,
		"GET",
		generateLicense,
		g.Map{
			"token": licenseToken,
			"seed":  mac,
		},
		tokenString,
	)
	if !licenseResp.Success || licenseResp.Data == nil || licenseResp.Data.SerialNumber == "" {
		t.Fatalf("generate device license failed, HTTP status=%d, response=%s", licenseStatus, licenseRaw)
	}
	t.Logf("generated device license, serial_number=%s", licenseResp.Data.SerialNumber)

	activateResp, activateRaw, activateStatus := directActivationTestActivateDevice(t, tokenString, *formattedMac, licenseResp.Data.SerialNumber)
	if !activateResp.Success {
		if !directActivationTestCanRebind(activateResp, activateStatus) {
			t.Fatalf("XiaoZhi activation failed, HTTP status=%d, response=%s", activateStatus, activateRaw)
		}
		directActivationTestUnbindBoundDeviceThenActivate(t, tokenString, *formattedMac, licenseResp.Data.SerialNumber)
	}
	t.Logf("XiaoZhi activation succeeded, mac=%s, serial_number=%s", *formattedMac, licenseResp.Data.SerialNumber)

	device := directActivationTestFindDevice(t, tokenString, *formattedMac, licenseResp.Data.SerialNumber)
	t.Cleanup(func() {
		directActivationTestUnbindDevice(t, tokenString, directActivationTestDeviceID(t, device), *formattedMac, licenseResp.Data.SerialNumber)
	})

	t.Logf(
		"activation confirmed, device_id=%d, id=%d, agent_id=%d, product_id=%d, mac=%s, serial_number=%s",
		device.DeviceID,
		device.ID,
		device.AgentID,
		device.ProductID,
		device.MacAddress,
		device.SerialNumber,
	)
}

func directActivationTestMac() string {
	mac := strings.TrimSpace(os.Getenv(directActivationTestMacEnv))
	if mac != "" {
		return mac
	}
	return defaultDirectActivationTestMac
}

func directActivationTestActivateDevice(t *testing.T, tokenString string, formattedMac string, serialNumber string) (xiaozhiModel.XiaoZhiResponse[xiaozhiModel.AgentsDevicesActivate], string, int) {
	t.Helper()

	return directActivationTestRequest[xiaozhiModel.AgentsDevicesActivate](
		t,
		"POST",
		deviceActivate,
		g.Map{
			"serial_number": serialNumber,
			"mac_address":   formattedMac,
		},
		tokenString,
	)
}

func directActivationTestCanRebind(resp xiaozhiModel.XiaoZhiResponse[xiaozhiModel.AgentsDevicesActivate], statusCode int) bool {
	return statusCode == http.StatusBadRequest && strings.TrimSpace(resp.Message) == directActivationBoundOtherSerialNumberError
}

func directActivationTestUnbindBoundDeviceThenActivate(t *testing.T, tokenString string, formattedMac string, serialNumber string) {
	t.Helper()

	t.Logf("XiaoZhi reports the device is bound to another serial number; querying by serial_number=%s, unbinding, then retrying activation", serialNumber)
	device := directActivationTestFindBoundDevice(t, tokenString, formattedMac, serialNumber)
	directActivationTestUnbindDevice(t, tokenString, directActivationTestDeviceID(t, device), device.MacAddress, device.SerialNumber)

	retryResp, retryRaw, retryStatus := directActivationTestActivateDevice(t, tokenString, formattedMac, serialNumber)
	if !retryResp.Success {
		t.Fatalf("XiaoZhi rebind failed, HTTP status=%d, response=%s", retryStatus, retryRaw)
	}
	t.Logf("XiaoZhi rebind succeeded, mac=%s, serial_number=%s", formattedMac, serialNumber)
}

func directActivationTestFindBoundDevice(t *testing.T, tokenString string, formattedMac string, serialNumber string) xiaozhiModel.Device {
	t.Helper()

	if device, ok := directActivationTestMaybeFindDevice(t, tokenString, g.Map{
		"page":          1,
		"pageSize":      10,
		"serial_number": serialNumber,
	}); ok {
		t.Logf("found bound device by serial number: device_id=%d, id=%d, mac=%s, serial_number=%s", device.DeviceID, device.ID, device.MacAddress, device.SerialNumber)
		return device
	}

	if device, ok := directActivationTestMaybeFindDevice(t, tokenString, g.Map{
		"page":        1,
		"pageSize":    10,
		"mac_address": formattedMac,
	}); ok {
		t.Logf("no device found by serial number; found bound device by MAC: device_id=%d, id=%d, mac=%s, serial_number=%s", device.DeviceID, device.ID, device.MacAddress, device.SerialNumber)
		return device
	}

	t.Fatalf("XiaoZhi reports the device is bound to another serial number, but no device to unbind was found by serial_number=%s or mac_address=%s", serialNumber, formattedMac)
	return xiaozhiModel.Device{}
}

func directActivationTestMaybeFindDevice(t *testing.T, tokenString string, params g.Map) (xiaozhiModel.Device, bool) {
	t.Helper()

	devicesResp, devicesRaw, devicesStatus := directActivationTestRequest[xiaozhiModel.ListData[xiaozhiModel.Device]](
		t,
		"GET",
		devices,
		params,
		tokenString,
	)
	if !devicesResp.Success || devicesResp.Data == nil {
		t.Fatalf("query XiaoZhi device failed, HTTP status=%d, response=%s", devicesStatus, devicesRaw)
	}
	if len(devicesResp.Data.List) == 0 {
		return xiaozhiModel.Device{}, false
	}
	return devicesResp.Data.List[0], true
}

func directActivationTestDeviceID(t *testing.T, device xiaozhiModel.Device) int {
	t.Helper()

	if device.DeviceID > 0 {
		return device.DeviceID
	}
	if device.ID > 0 {
		return device.ID
	}
	t.Fatalf("XiaoZhi returned a device without a valid unbind ID: device_id=%d, id=%d, mac=%s, serial_number=%s", device.DeviceID, device.ID, device.MacAddress, device.SerialNumber)
	return 0
}

func directActivationTestGetToken(t *testing.T) string {
	t.Helper()

	secretKey := strings.TrimSpace(g.Cfg().MustGet(ctx, "xiaozhi.secret_key").String())
	if secretKey == "" {
		t.Fatal("xiaozhi.secret_key is empty; cannot get a XiaoZhi developer token")
	}

	tokenResp, tokenRaw, tokenStatus := directActivationTestRequest[ZhiGetToken](
		t,
		"POST",
		tokenPath,
		g.Map{
			"secret_key": secretKey,
		},
		"",
	)
	if !tokenResp.Success || tokenResp.Data == nil || tokenResp.Data.Token == "" {
		t.Fatalf("get XiaoZhi developer token failed, HTTP status=%d, response=%s", tokenStatus, tokenRaw)
	}
	t.Log("got XiaoZhi developer token")
	return tokenResp.Data.Token
}

func directActivationTestFindDevice(t *testing.T, tokenString string, formattedMac string, serialNumber string) xiaozhiModel.Device {
	t.Helper()

	devicesResp, devicesRaw, devicesStatus := directActivationTestRequest[xiaozhiModel.ListData[xiaozhiModel.Device]](
		t,
		"GET",
		devices,
		g.Map{
			"page":          1,
			"pageSize":      10,
			"mac_address":   formattedMac,
			"serial_number": serialNumber,
		},
		tokenString,
	)
	if !devicesResp.Success || devicesResp.Data == nil {
		t.Fatalf("post-activation confirmation query failed, HTTP status=%d, response=%s", devicesStatus, devicesRaw)
	}
	if len(devicesResp.Data.List) == 0 {
		t.Fatalf("no XiaoZhi device found after activation, HTTP status=%d, response=%s", devicesStatus, devicesRaw)
	}
	return devicesResp.Data.List[0]
}

func directActivationTestUnbindDevice(t *testing.T, tokenString string, deviceID int, formattedMac string, serialNumber string) {
	t.Helper()

	if deviceID < 1 {
		t.Fatalf("invalid XiaoZhi unbind device_id: device_id=%d, mac=%s, serial_number=%s", deviceID, formattedMac, serialNumber)
	}

	unbindResp, unbindRaw, unbindStatus := directActivationTestRequest[model.Empty](
		t,
		"POST",
		deviceUnbind,
		g.Map{
			"device_id": deviceID,
		},
		tokenString,
	)
	if !unbindResp.Success && unbindResp.Message != "device not found" {
		t.Fatalf("activation succeeded but XiaoZhi unbind failed, HTTP status=%d, response=%s", unbindStatus, unbindRaw)
	}
	t.Logf("XiaoZhi unbind succeeded, device_id=%d, mac=%s, serial_number=%s", deviceID, formattedMac, serialNumber)
}

func directActivationTestRequest[T any](t *testing.T, method string, path string, data interface{}, tokenString string) (xiaozhiModel.XiaoZhiResponse[T], string, int) {
	t.Helper()

	client := g.Client()
	client.SetTimeout(10 * time.Second)
	client.SetContentType("application/json")
	if tokenString != "" {
		client.SetHeader("Authorization", "Bearer "+tokenString)
	}

	var requestURL = baseUrl + path
	if strings.EqualFold(method, "GET") {
		if params, ok := data.(g.Map); ok && len(params) > 0 {
			queryValues := url.Values{}
			for k, v := range params {
				queryValues.Set(k, fmt.Sprint(v))
			}
			requestURL += "?" + queryValues.Encode()
		}
	}

	var (
		clientResp *gclient.Response
		err        error
	)
	switch strings.ToUpper(method) {
	case "GET":
		resp, reqErr := client.Get(ctx, requestURL)
		clientResp = resp
		err = reqErr
	case "POST":
		resp, reqErr := client.ContentJson().Post(ctx, requestURL, data)
		clientResp = resp
		err = reqErr
	default:
		t.Fatalf("unsupported request method: %s", method)
	}
	if err != nil {
		t.Fatalf("XiaoZhi API request failed: method=%s, path=%s, error=%v", strings.ToUpper(method), path, err)
	}
	if clientResp == nil || clientResp.Response == nil {
		t.Fatalf("XiaoZhi API request failed: method=%s, path=%s, empty response", strings.ToUpper(method), path)
	}
	defer clientResp.Close()

	respBody := clientResp.ReadAllString()
	var parsed xiaozhiModel.XiaoZhiResponse[T]
	if err := gjson.New(respBody).Scan(&parsed); err != nil {
		t.Fatalf("parse XiaoZhi API response failed: method=%s, path=%s, HTTP status=%d, response=%s, error=%v", strings.ToUpper(method), path, clientResp.StatusCode, respBody, err)
	}
	return parsed, respBody, clientResp.StatusCode
}
