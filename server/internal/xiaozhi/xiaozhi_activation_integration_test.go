/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

package xiaozhi

import (
	"errors"
	"os"
	"strings"
	"testing"

	"github.com/gogf/gf/v2/frame/g"
)

// Placeholder MAC; override it with STACKCHAN_TEST_MAC before running.
const defaultActivationIntegrationTestMac = "00:11:22:33:44:55"

func TestActivateDeviceThenUnbind(t *testing.T) {
	if os.Getenv("STACKCHAN_RUN_XIAOZHI_ACTIVATION_TEST") != "1" {
		t.Skip("set STACKCHAN_RUN_XIAOZHI_ACTIVATION_TEST=1 to call the real XiaoZhi activation API")
	}

	requireXiaoZhiActivationTestConfig(t)

	mac := strings.TrimSpace(os.Getenv("STACKCHAN_TEST_MAC"))
	if mac == "" {
		mac = defaultActivationIntegrationTestMac
	}

	formattedMac := formatMac(&mac)
	if formattedMac == nil {
		t.Fatalf("invalid mac address: %s", mac)
	}

	license, err := GenerateLicense(mac)
	if err != nil {
		t.Fatalf("generate license failed, mac=%s, reason=%v", mac, err)
	}
	if license.SerialNumber == "" {
		t.Fatalf("generate license returned empty serial number, mac=%s", mac)
	}
	t.Logf("generated license, mac=%s, formatted_mac=%s, serial_number=%s", mac, *formattedMac, license.SerialNumber)

	activated, err := ActivateDevice(license.SerialNumber, *formattedMac)
	if err != nil {
		if errors.Is(err, ErrDeviceAlreadyAdded) {
			t.Fatalf("activation failed because XiaoZhi reported this device has already been added, mac=%s, serial_number=%s, reason=%v", *formattedMac, license.SerialNumber, err)
		}
		t.Fatalf("activation failed, mac=%s, serial_number=%s, reason=%v", *formattedMac, license.SerialNumber, err)
	}
	if !activated {
		t.Fatalf("activation returned false, mac=%s, serial_number=%s", *formattedMac, license.SerialNumber)
	}
	t.Logf("activation succeeded, mac=%s, serial_number=%s", *formattedMac, license.SerialNumber)

	t.Cleanup(func() {
		unbound, err := UnbindDevice(formattedMac)
		if err != nil {
			t.Errorf("activation succeeded but XiaoZhi unbind failed, mac=%s, serial_number=%s, reason=%v", *formattedMac, license.SerialNumber, err)
			return
		}
		if !unbound {
			t.Errorf("activation succeeded but XiaoZhi unbind returned false, mac=%s, serial_number=%s", *formattedMac, license.SerialNumber)
			return
		}
		t.Logf("unbind succeeded, mac=%s, serial_number=%s", *formattedMac, license.SerialNumber)
	})

	devices, err := GetDevices(nil, nil, formattedMac, &license.SerialNumber, nil, nil)
	if err != nil {
		t.Fatalf("activation confirmation query failed, mac=%s, serial_number=%s, reason=%v", *formattedMac, license.SerialNumber, err)
	}
	if devices == nil || len(*devices) == 0 {
		t.Fatalf("activation confirmation found no XiaoZhi device, mac=%s, serial_number=%s", *formattedMac, license.SerialNumber)
	}

	device := (*devices)[0]
	t.Logf("activation confirmed, device_id=%d, agent_id=%d, product_id=%d, mac=%s, serial_number=%s", device.DeviceID, device.AgentID, device.ProductID, device.MacAddress, device.SerialNumber)
}

func requireXiaoZhiActivationTestConfig(t *testing.T) {
	t.Helper()

	var (
		secretKey            = strings.TrimSpace(g.Cfg().MustGet(ctx, "xiaozhi.secret_key").String())
		generateLicenseToken = strings.TrimSpace(g.Cfg().MustGet(ctx, "xiaozhi.generate_license_token").String())
	)
	if secretKey == "" {
		t.Fatal("xiaozhi.secret_key is empty")
	}
	if generateLicenseToken == "" {
		t.Fatal("xiaozhi.generate_license_token is empty")
	}
}
