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

// User is the golang structure of table user for DAO operations like Where/Data.
type User struct {
	g.Meta         `orm:"table:user, do:true"`
	Uid            any         // User unique UID (remote platform primary key)
	Username       any         // Login username
	Userslug       any         // User alias
	DisplayName    any         // User display name
	IconText       any         // User icon text
	IconBgColor    any         // Icon background color
	EmailConfirmed any         // Email verified, 0-no 1-yes
	JoinDate       any         // Registration timestamp (milliseconds)
	LastOnline     any         // Last online timestamp (milliseconds)
	UserStatus     any         // User online status
	CreateAt       *gtime.Time // Local creation time
	UpdateAt       *gtime.Time // Local update time
	IsDeleted      any         // Is deleted, 0-normal 1-deleted
}
