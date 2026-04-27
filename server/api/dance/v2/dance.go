/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

package v2

import (
	"encoding/json"
	"stackChan/internal/model"

	"github.com/gogf/gf/v2/frame/g"
)

type GetListReq struct {
	g.Meta `path:"/dance" method:"get" tags:"Dance" summary:"Dance get request"`
	Mac    string `json:"mac" v:"required"` // mac address
}

type GetListRes []model.Dance

type CreateReq struct {
	g.Meta    `path:"/dance" method:"post" tags:"Dance" summary:"Dance create request"`
	Mac       string          `json:"mac" v:"required"`       // mac address
	DanceData json.RawMessage `json:"danceData"`              // Dance motion data
	DanceName string          `json:"danceName" v:"required"` // Dance name
	MusicUrl  string          `json:"musicUrl"`               // Dance background music URL
}

type CreateRes string

type DeleteReq struct {
	g.Meta `path:"/dance" method:"delete" tags:"Dance" summary:"Dance delete request"`
	Id     int64 `json:"id" v:"required"`
}

type DeleteRes string

type UpdateReq struct {
	g.Meta    `path:"/dance" method:"put" tags:"Dance" summary:"Dance put request"`
	Id        int64           `json:"id" v:"required"`
	DanceData json.RawMessage `json:"danceData"` // Dance motion data
	DanceName string          `json:"danceName"` // Dance name
	MusicUrl  string          `json:"musicUrl"`  // Dance background music URL
}

type UpdateRes string

type GetDanceInfoReq struct {
	g.Meta `path:"/danceData" method:"get" tags:"Dance get request"`
	Id     int64 `json:"id" v:"required"`
}

type GetDanceInfoRes model.Dance
