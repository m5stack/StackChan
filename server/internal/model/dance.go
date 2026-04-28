/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

package model

import "encoding/json"

type Dance struct {
	Id        int64           `json:"id"         orm:"id"          description:"Dance ID"`                   //
	DanceName string          `json:"danceName"  orm:"dance_name"  description:"Dance name"`                 // Dance name
	MusicUrl  string          `json:"musicUrl"   orm:"music_url"   description:"Dance background music URL"` // Dance background music URL
	DanceData json.RawMessage `json:"danceData"  orm:"dance_data"  description:"MotionData"`                 // Dance motion data
}
