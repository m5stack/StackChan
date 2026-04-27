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
)

// Device is the golang structure of table device for DAO operations like Where/Data.
type Device struct {
	g.Meta   `orm:"table:device, do:true"`
	Mac      any //
	Name     any //
	Uid      any // Bound user UID
	BindTime any // Device binding time
}
