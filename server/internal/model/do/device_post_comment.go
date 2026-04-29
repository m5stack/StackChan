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

// DevicePostComment is the golang structure of table device_post_comment for DAO operations like Where/Data.
type DevicePostComment struct {
	g.Meta    `orm:"table:device_post_comment, do:true"`
	Id        any         //
	PostId    any         // Post ID
	Mac       any         // Comment device MAC
	Content   any         //
	CreatedAt *gtime.Time // Comment time
}
