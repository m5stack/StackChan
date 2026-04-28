/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

package v1

import (
	"encoding/json"
	"stackChan/internal/model"

	"github.com/gogf/gf/v2/frame/g"
)

type CreateReq struct {
	g.Meta    `path:"/dance" method:"post" tags:"Dance" summary:"Dance create request"`
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

type GetListReq struct {
	g.Meta `path:"/dance" method:"get" tags:"Dance" summary:"Dance get request"`
}

type GetListRes []model.Dance

type GetDanceInfoReq struct {
	g.Meta `path:"/danceData" method:"get" tags:"Dance get request"`
	Id     int64 `json:"id" v:"required"`
}

type GetDanceInfoRes model.Dance

type GetMusicListReq struct {
	g.Meta `path:"/musicList" method:"get" tags:"Dance get request"`
}

type GetMusicListRes []string
