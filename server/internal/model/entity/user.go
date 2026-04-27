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

// User is the golang structure for table user.
type User struct {
	Uid            int64       `json:"uid"            orm:"uid"             description:"User unique UID (remote platform primary key)"` // User unique UID (remote platform primary key)
	Username       string      `json:"username"       orm:"username"        description:"Login username"`                                // Login username
	Userslug       string      `json:"userslug"       orm:"userslug"        description:"User alias"`                                    // User alias
	DisplayName    string      `json:"displayName"    orm:"display_name"    description:"User display name"`                             // User display name
	IconText       string      `json:"iconText"       orm:"icon_text"       description:"User icon text"`                                // User icon text
	IconBgColor    string      `json:"iconBgColor"    orm:"icon_bg_color"   description:"Icon background color"`                         // Icon background color
	EmailConfirmed int         `json:"emailConfirmed" orm:"email_confirmed" description:"Email verified, 0-no 1-yes"`                    // Email verified, 0-no 1-yes
	JoinDate       int64       `json:"joinDate"       orm:"join_date"       description:"Registration timestamp (milliseconds)"`         // Registration timestamp (milliseconds)
	LastOnline     int64       `json:"lastOnline"     orm:"last_online"     description:"Last online timestamp (milliseconds)"`          // Last online timestamp (milliseconds)
	UserStatus     string      `json:"userStatus"     orm:"user_status"     description:"User online status"`                            // User online status
	CreateAt       *gtime.Time `json:"createAt"       orm:"create_at"       description:"Local creation time"`                           // Local creation time
	UpdateAt       *gtime.Time `json:"updateAt"       orm:"update_at"       description:"Local update time"`                             // Local update time
	IsDeleted      int         `json:"isDeleted"      orm:"is_deleted"      description:"Is deleted, 0-normal 1-deleted"`                // Is deleted, 0-normal 1-deleted
}
