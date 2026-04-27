/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

// =================================================================================
// Code generated and maintained by GoFrame CLI tool. DO NOT EDIT.
// =================================================================================

package entity

import (
	"github.com/gogf/gf/v2/os/gtime"
)

// DeviceDance is the golang structure for table device_dance.
type DeviceDance struct {
	Id        int64       `json:"id"        orm:"id"         description:""`                           //
	Mac       string      `json:"mac"       orm:"mac"        description:"Device MAC address"`         // Device MAC address
	DanceName string      `json:"danceName" orm:"dance_name" description:"Dance name"`                 // Dance name
	DanceData string      `json:"danceData" orm:"dance_data" description:"MotionData"`                 // MotionData
	MusicUrl  string      `json:"musicUrl"  orm:"music_url"  description:"Dance background music URL"` // Dance background music URL
	CreatedAt *gtime.Time `json:"createdAt" orm:"created_at" description:""`                           //
	UpdatedAt *gtime.Time `json:"updatedAt" orm:"updated_at" description:""`                           //
}
