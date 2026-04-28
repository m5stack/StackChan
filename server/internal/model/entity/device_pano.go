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

// DevicePano is the golang structure for table device_pano.
type DevicePano struct {
	Id        int64       `json:"id"        orm:"id"         description:""`                   //
	Mac       string      `json:"mac"       orm:"mac"        description:"Device MAC address"` // Device MAC address
	PanoUrl   string      `json:"panoUrl"   orm:"pano_url"   description:"Panorama URL"`       // Panorama URL
	CreatedAt *gtime.Time `json:"createdAt" orm:"created_at" description:"Creation time"`      // Creation time
	UpdatedAt *gtime.Time `json:"updatedAt" orm:"updated_at" description:""`                   //
}
