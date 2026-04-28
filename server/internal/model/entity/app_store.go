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

// AppStore is the golang structure for table app_store.
type AppStore struct {
	Id          int64       `json:"id"          orm:"id"           description:""`                                             //
	AppName     string      `json:"appName"     orm:"app_name"     description:"App name"`                                     // App name
	AppIconUrl  string      `json:"appIconUrl"  orm:"app_icon_url" description:"App icon URL"`                                 // App icon URL
	Description string      `json:"description" orm:"description"  description:"App description"`                              // App description
	FirmwareUrl string      `json:"firmwareUrl" orm:"firmware_url" description:"Firmware / installation package download URL"` // Firmware / installation package download URL
	CreateAt    *gtime.Time `json:"createAt"    orm:"create_at"    description:"Creation time"`                                // Creation time
	UpdateAt    *gtime.Time `json:"updateAt"    orm:"update_at"    description:"Update time"`                                  // Update time
	IsDeleted   int         `json:"isDeleted"   orm:"is_deleted"   description:"Is deleted, 0 normal 1 deleted"`               // Is deleted, 0 normal 1 deleted
}
