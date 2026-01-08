/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

package model

type DeviceInfo struct {
	Mac  string `json:"mac" v:"required" description:"Mac address"`
	Name string `json:"name" v:"required" description:"Name"`
}
