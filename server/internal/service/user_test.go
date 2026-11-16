/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

package service

import (
	"strings"
	"testing"
)

func TestIsRemoteAccountUnexpectedResponse(t *testing.T) {
	tests := []struct {
		name       string
		statusCode int
		body       string
		want       bool
	}{
		{
			name:       "bad gateway",
			statusCode: 502,
			body:       "<html><head><title>502 Bad Gateway</title></head></html>",
			want:       true,
		},
		{
			name:       "html error with ok status",
			statusCode: 200,
			body:       "<!doctype html><html><body>error</body></html>",
			want:       true,
		},
		{
			name:       "business json error",
			statusCode: 400,
			body:       `{"status":{"code":"bad-request","message":"Username already exists"}}`,
			want:       false,
		},
		{
			name:       "business bracket error",
			statusCode: 200,
			body:       "[[error:invalid-email]]",
			want:       false,
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			if got := isRemoteAccountUnexpectedResponse(tt.statusCode, tt.body); got != tt.want {
				t.Fatalf("isRemoteAccountUnexpectedResponse() = %v, want %v", got, tt.want)
			}
		})
	}
}

func TestValidateRemoteUsernameQueryResponse(t *testing.T) {
	tests := []struct {
		name       string
		username   string
		statusCode int
		body       string
		wantErr    bool
		wantMsg    string
	}{
		{
			name:       "available response passes",
			username:   "new-user",
			statusCode: 200,
			body:       `{"status":{"code":"ok","message":"OK"},"response":{"available":true}}`,
			wantErr:    false,
		},
		{
			name:       "user not found passes",
			username:   "new-user",
			statusCode: 404,
			body:       `{"status":{"code":"not-found","message":"[[error:no-user]]"}}`,
			wantErr:    false,
		},
		{
			name:       "existing user fails",
			username:   "taken",
			statusCode: 200,
			body:       `{"status":{"code":"ok","message":"OK"},"response":{"uid":123,"username":"taken"}}`,
			wantErr:    true,
			wantMsg:    usernameCheckFailedMessage,
		},
		{
			name:       "root user object fails",
			username:   "existing-user",
			statusCode: 200,
			body:       `{"uid":123456,"username":"existing-user","userslug":"existing-user","email":"user@example.com","status":"online"}`,
			wantErr:    true,
			wantMsg:    usernameCheckFailedMessage,
		},
		{
			name:       "remote business failure keeps reminder",
			username:   "taken",
			statusCode: 400,
			body:       `{"status":{"code":"bad-request","message":"Username already exists"}}`,
			wantErr:    true,
			wantMsg:    "Please change the username and try again.",
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			err := validateRemoteUsernameQueryResponse(tt.username, tt.statusCode, tt.body)
			if tt.wantErr && err == nil {
				t.Fatal("validateRemoteUsernameQueryResponse() error = nil, want error")
			}
			if !tt.wantErr && err != nil {
				t.Fatalf("validateRemoteUsernameQueryResponse() error = %v, want nil", err)
			}
			if tt.wantMsg != "" && !strings.Contains(err.Error(), tt.wantMsg) {
				t.Fatalf("validateRemoteUsernameQueryResponse() error = %q, want to contain %q", err.Error(), tt.wantMsg)
			}
		})
	}
}
