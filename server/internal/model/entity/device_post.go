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

// DevicePost is the golang structure for table device_post.
type DevicePost struct {
	Id           int64       `json:"id"           orm:"id"            description:""`                //
	Mac          string      `json:"mac"          orm:"mac"           description:"Post device MAC"` // Post device MAC
	ContentText  string      `json:"contentText"  orm:"content_text"  description:""`                //
	ContentImage string      `json:"contentImage" orm:"content_image" description:"Image URL"`       // Image URL
	CreatedAt    *gtime.Time `json:"createdAt"    orm:"created_at"    description:"Post time"`       // Post time
}
