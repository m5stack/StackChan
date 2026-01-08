/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

package v1

import (
	"github.com/gogf/gf/v2/frame/g"
	"github.com/gogf/gf/v2/net/ghttp"
)

type FileReq struct {
	g.Meta    `path:"/uploadFile" method:"post" tags:"File" summary:"File upload request"`
	File      *ghttp.UploadFile `json:"file" v:"required" description:"File upload request"`
	Name      string            `json:"name" v:"required" description:"File Name"`
	Directory string            `json:"directory" description:"Directory upload request"`
}

type FileRes struct {
	Path string `json:"path" description:"file path"`
}
