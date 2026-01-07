/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

package model

import "github.com/gogf/gf/v2/os/gtime"

type Post struct {
	Id              int64          `json:"id"           orm:"id"            description:"帖子ID"`
	Mac             string         `json:"mac"          orm:"mac"           description:"发帖设备MAC"`
	Name            string         `json:"name"         orm:"name"          description:"发帖设备名称"`
	ContentText     string         `json:"contentText"  orm:"content_text"  description:"文本内容"`
	ContentImage    string         `json:"contentImage" orm:"content_image" description:"图片URL"`
	CreatedAt       *gtime.Time    `json:"createdAt"    orm:"created_at"    description:"发帖时间"`
	PostCommentList []*PostComment `json:"postCommentList" orm:"postCommentList" description:"评论"`
}

type PostComment struct {
	Id        int         `json:"id"        orm:"id"         description:""` //
	PostId    int         `json:"postId"    orm:"post_id"    description:""` //
	Mac       string      `json:"mac"       orm:"mac"        description:""` //
	Name      string      `json:"name"      orm:"name"       description:""` //
	Content   string      `json:"content"   orm:"content"    description:""` //
	CreatedAt *gtime.Time `json:"createdAt" orm:"created_at" description:""` //
}
