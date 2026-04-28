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

// DevicePostComment is the golang structure for table device_post_comment.
type DevicePostComment struct {
	Id        int64       `json:"id"        orm:"id"         description:""`                   //
	PostId    int64       `json:"postId"    orm:"post_id"    description:"Post ID"`            // Post ID
	Mac       string      `json:"mac"       orm:"mac"        description:"Comment device MAC"` // Comment device MAC
	Content   string      `json:"content"   orm:"content"    description:""`                   //
	CreatedAt *gtime.Time `json:"createdAt" orm:"created_at" description:"Comment time"`       // Comment time
}
