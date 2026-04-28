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

// AppStore is the golang structure of table app_store for DAO operations like Where/Data.
type AppStore struct {
	g.Meta      `orm:"table:app_store, do:true"`
	Id          any         //
	AppName     any         // App name
	AppIconUrl  any         // App icon URL
	Description any         // App description
	FirmwareUrl any         // Firmware / installation package download URL
	CreateAt    *gtime.Time // Creation time
	UpdateAt    *gtime.Time // Update time
	IsDeleted   any         // Is deleted, 0 normal 1 deleted
}
