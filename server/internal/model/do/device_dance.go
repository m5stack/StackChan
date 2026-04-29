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

// DeviceDance is the golang structure of table device_dance for DAO operations like Where/Data.
type DeviceDance struct {
	g.Meta    `orm:"table:device_dance, do:true"`
	Id        any         //
	Mac       any         // Device MAC address
	DanceName any         // Dance name
	DanceData any         // MotionData
	MusicUrl  any         // Dance background music URL
	CreatedAt *gtime.Time //
	UpdatedAt *gtime.Time //
}
