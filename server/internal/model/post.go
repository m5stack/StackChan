/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

package model

import "github.com/gogf/gf/v2/os/gtime"

type Post struct {
	Id              int64          `json:"id"           orm:"id"            description:"Post ID"`
	Mac             string         `json:"mac"          orm:"mac"           description:"Post device MAC"`
	Name            string         `json:"name"         orm:"name"          description:"Post device name"`
	ContentText     string         `json:"contentText"  orm:"content_text"  description:"Text content"`
	ContentImage    string         `json:"contentImage" orm:"content_image" description:"Image URL"`
	CreatedAt       *gtime.Time    `json:"createdAt"    orm:"created_at"    description:"Post time"`
	PostCommentList []*PostComment `json:"postCommentList" orm:"postCommentList" description:"Comments"`
}

type PostComment struct {
	Id        int         `json:"id"        orm:"id"         description:""` //
	PostId    int         `json:"postId"    orm:"post_id"    description:""` //
	Mac       string      `json:"mac"       orm:"mac"        description:""` //
	Name      string      `json:"name"      orm:"name"       description:""` //
	Content   string      `json:"content"   orm:"content"    description:""` //
	CreatedAt *gtime.Time `json:"createdAt" orm:"created_at" description:""` //
}
