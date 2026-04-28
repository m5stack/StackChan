/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

package v1

import (
	"stackChan/internal/model"

	"github.com/gogf/gf/v2/frame/g"
)

type AddPanoReq struct {
	g.Meta `path:"/pano" method:"post" tags:"Pano" summary:"Pano add request"`
	Url    string `json:"url" v:"required" description:"Pano image url"`
}

type AddPanoRes struct {
	Id int64 `json:"id"`
}

type GetPanoListReq struct {
	g.Meta `path:"/pano" method:"get" tags:"Pano" summary:"Pano list"`
}

type GetPanoListRes []model.Pano
