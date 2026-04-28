/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

package model

type DeviceInfo struct {
	Mac      string `json:"mac" v:"required" description:"Mac address"`
	Name     string `json:"name" v:"required" description:"Name"`
	Uid      int64  `json:"uid"  orm:"uid"  description:"Bound user UID"`                // Bound user UID
	BindTime string `json:"bind_time" orm:"bind_time" description:"Device binding time"` // Device binding time
}

type IPLocation struct {
	Status      string  `json:"status"`
	Country     string  `json:"country"`
	CountryCode string  `json:"countryCode"`
	Region      string  `json:"region"`
	RegionName  string  `json:"regionName"`
	City        string  `json:"city"`
	Zip         string  `json:"zip"`
	Lat         float64 `json:"lat"`
	Lon         float64 `json:"lon"`
	TimeZone    string  `json:"timezone"`
	Isp         string  `json:"isp"`
	Org         string  `json:"org"`
	As          string  `json:"as"`
	Query       string  `json:"query"`
}
