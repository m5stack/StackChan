/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

// =================================================================================
// Code generated and maintained by GoFrame CLI tool. DO NOT EDIT.
// =================================================================================

package do

import (
	"github.com/gogf/gf/v2/frame/g"
	"github.com/gogf/gf/v2/os/gtime"
)

// DevicePano is the golang structure of table device_pano for DAO operations like Where/Data.
type DevicePano struct {
	g.Meta    `orm:"table:device_pano, do:true"`
	Id        any         //
	Mac       any         // Device MAC address
	PanoUrl   any         // Panorama URL
	CreatedAt *gtime.Time // Creation time
	UpdatedAt *gtime.Time //
}
