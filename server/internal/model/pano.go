/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

package model

import "github.com/gogf/gf/v2/os/gtime"

type Pano struct {
	Id        int64       `json:"id"        orm:"id"         description:""`              //
	PanoUrl   string      `json:"panoUrl"   orm:"pano_url"   description:"Panorama URL"`  // Panorama URL
	CreatedAt *gtime.Time `json:"createdAt" orm:"created_at" description:"Creation time"` // Creation time
	UpdatedAt *gtime.Time `json:"updatedAt" orm:"updated_at" description:""`              //
}
