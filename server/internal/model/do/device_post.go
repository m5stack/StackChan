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

// DevicePost is the golang structure of table device_post for DAO operations like Where/Data.
type DevicePost struct {
	g.Meta       `orm:"table:device_post, do:true"`
	Id           any         //
	Mac          any         // Post device MAC
	ContentText  any         //
	ContentImage any         // Image URL
	CreatedAt    *gtime.Time // Post time
}
