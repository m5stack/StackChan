/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

package v1

import (
	"stackChan/internal/model"

	"github.com/gogf/gf/v2/frame/g"
)

type CreateReq struct {
	g.Meta `path:"/dance" method:"post" tags:"Dance" summary:"Dance create request"`
	Mac    string            `json:"mac" v:"required"`
	Index  int               `json:"index" v:"required"`
	List   []model.DanceData `json:"list" v:"required"`
}

type CreateRes string

type DeleteReq struct {
	g.Meta `path:"/dance" method:"delete" tags:"Dance" summary:"Dance delete request"`
	Mac    string `json:"mac" v:"required"`
	Index  int    `json:"index" v:"required"`
}

type DeleteRes string

type UpdateReq struct {
	g.Meta `path:"/dance" method:"put" tags:"Dance" summary:"Dance put request"`
	Mac    string            `json:"mac" v:"required"`
	Index  int               `json:"index" v:"required"`
	Data   []model.DanceData `json:"list" v:"required"`
}

type UpdateRes string

type GetListReq struct {
	g.Meta `path:"/dance" method:"get" tags:"Dance" summary:"Dance get request"`
	Mac    string `json:"mac" v:"required"`
}

type GetListRes map[string][]model.DanceData
