/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

// =================================================================================
// Code generated and maintained by GoFrame CLI tool. DO NOT EDIT.
// =================================================================================

package entity

// Device is the golang structure for table device.
type Device struct {
	Mac      string `json:"mac"      orm:"mac"       description:""`                    //
	Name     string `json:"name"     orm:"name"      description:""`                    //
	Uid      int64  `json:"uid"      orm:"uid"       description:"Bound user UID"`      // Bound user UID
	BindTime string `json:"bindTime" orm:"bind_time" description:"Device binding time"` // Device binding time
}
