/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

package service

import (
	"context"
	"fmt"
	"net/http"
	"net/http/httptest"
	"testing"

	"github.com/gogf/gf/v2/errors/gcode"
	"github.com/gogf/gf/v2/errors/gerror"
	"github.com/gogf/gf/v2/frame/g"
	"github.com/gogf/gf/v2/os/gcfg"
)

func TestCreateMac(t *testing.T) {
}

func TestValidateM5StackDeviceAcceptsOKResponse(t *testing.T) {
	const (
		mac   = "AABBCCDDEEFF"
		token = "test-token"
	)

	var (
		gotPath string
		gotAuth string
	)
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		gotPath = r.URL.EscapedPath()
		gotAuth = r.Header.Get("Authorization")
		_, _ = w.Write([]byte(m5StackVerificationOKBody))
	}))
	defer server.Close()

	setM5StackVerificationConfig(t, server.URL+"/", token)

	err := validateM5StackDevice(context.Background(), mac)
	if err != nil {
		t.Fatalf("validateM5StackDevice() error = %v, want nil", err)
	}
	if gotPath != "/"+mac {
		t.Fatalf("verification request path = %q, want %q", gotPath, "/"+mac)
	}
	if gotAuth != token {
		t.Fatalf("verification request Authorization = %q, want %q", gotAuth, token)
	}
}

func TestValidateM5StackDeviceRejectsNonStackChanMac(t *testing.T) {
	tests := []struct {
		name       string
		statusCode int
		body       string
	}{
		{
			name:       "non ok status",
			statusCode: http.StatusBadRequest,
			body:       "Bad Request",
		},
		{
			name:       "ok status with non ok body",
			statusCode: http.StatusOK,
			body:       "Bad Request",
		},
		{
			name:       "ok status with extra body whitespace",
			statusCode: http.StatusOK,
			body:       "OK\n",
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			err := validateM5StackDeviceWithResponse(t, "AABBCCDDEEFF", tt.statusCode, tt.body)
			assertNonStackChanMainControllerError(t, err)
		})
	}
}

func TestValidateM5StackDeviceReturnsNetworkQueryFailed(t *testing.T) {
	t.Run("server error status", func(t *testing.T) {
		err := validateM5StackDeviceWithResponse(t, "AABBCCDDEEFF", http.StatusInternalServerError, "Internal Server Error")
		assertM5StackVerificationFailedError(t, err)
	})

	t.Run("connection failed", func(t *testing.T) {
		server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {}))
		verificationURL := server.URL + "/"
		server.Close()

		setM5StackVerificationConfig(t, verificationURL, "test-token")

		err := validateM5StackDevice(context.Background(), "AABBCCDDEEFF")
		assertM5StackVerificationFailedError(t, err)
	})
}

func validateM5StackDeviceWithResponse(t *testing.T, mac string, statusCode int, body string) error {
	t.Helper()

	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.WriteHeader(statusCode)
		_, _ = w.Write([]byte(body))
	}))
	defer server.Close()

	setM5StackVerificationConfig(t, server.URL+"/", "test-token")

	return validateM5StackDevice(context.Background(), mac)
}

func assertNonStackChanMainControllerError(t *testing.T, err error) {
	t.Helper()

	if err == nil {
		t.Fatal("validateM5StackDevice() error = nil, want non-stackchan error")
	}
	if got := gerror.Code(err); got != gcode.CodeBusinessValidationFailed {
		t.Fatalf("validateM5StackDevice() code = %d, want %d", got.Code(), gcode.CodeBusinessValidationFailed.Code())
	}
	if err.Error() != nonStackChanMainControllerMessage {
		t.Fatalf("validateM5StackDevice() error = %q, want %q", err.Error(), nonStackChanMainControllerMessage)
	}
}

func assertM5StackVerificationFailedError(t *testing.T, err error) {
	t.Helper()

	if err == nil {
		t.Fatal("validateM5StackDevice() error = nil, want verification failed error")
	}
	if got := gerror.Code(err); got != gcode.CodeInternalError {
		t.Fatalf("validateM5StackDevice() code = %d, want %d", got.Code(), gcode.CodeInternalError.Code())
	}
	if err.Error() != m5StackVerificationFailedMessage {
		t.Fatalf("validateM5StackDevice() error = %q, want %q", err.Error(), m5StackVerificationFailedMessage)
	}
}

func setM5StackVerificationConfig(t *testing.T, verificationURL string, token string) {
	t.Helper()

	setTestLogger(t)

	previousAdapter := g.Cfg().GetAdapter()
	adapter, err := gcfg.NewAdapterContent(fmt.Sprintf(`
m5stack:
  queryMacAddressUrl: %q
  queryMacAddressToken: %q
`, verificationURL, token))
	if err != nil {
		t.Fatalf("gcfg.NewAdapterContent() error = %v", err)
	}

	g.Cfg().SetAdapter(adapter)
	t.Cleanup(func() {
		g.Cfg().SetAdapter(previousAdapter)
	})
}

func setTestLogger(t *testing.T) {
	t.Helper()

	logger := g.Log()
	previousConfig := logger.GetConfig()
	config := previousConfig
	config.Path = t.TempDir()
	config.StdoutPrint = false
	if err := logger.SetConfig(config); err != nil {
		t.Fatalf("logger.SetConfig() error = %v", err)
	}
	t.Cleanup(func() {
		_ = logger.SetConfig(previousConfig)
	})
}
