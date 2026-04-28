/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

package v1

import (
	"stackChan/internal/model"

	"github.com/gogf/gf/v2/frame/g"
)

type CreatePostReq struct {
	g.Meta       `path:"/post/add" method:"post" tags:"Post" summary:"Post create request"`
	ContentText  string `json:"content_text" v:"required" description:"Content text"`
	ContentImage string `json:"content_image" description:"Content image"`
}

type CreatePostRes struct {
	Id int64 `json:"id"`
}

type GetPostReq struct {
	g.Meta   `path:"/post/get" method:"get" tags:"Post" summary:"Post get request"`
	Page     int `json:"page"    v:"required#Page cannot be empty" description:"Page number"`
	PageSize int `json:"pageSize" v:"required#Page size cannot be empty" description:"Items per page"`
}

type GetPostRes []model.Post

type DeletePostReq struct {
	g.Meta `path:"/post/delete" method:"delete" tags:"Post" summary:"Post delete request"`
	Id     int `json:"id" summary:"Post id"`
}

type DeletePostRes string

type CreatePostCommentReq struct {
	g.Meta  `path:"/post/comment/create" method:"post" tags:"Post" summary:"Post create comment"`
	PostId  int64  `json:"postId" v:"required" summary:"Post comment id"`
	Content string `json:"content"     description:"Comment content"`
}

type CreatePostCommentRes struct {
	Id int64 `json:"id"`
}

type DeletePostCommentReq struct {
	g.Meta    `path:"/post/comment/delete" method:"delete" tags:"Post" summary:"Post delete comment"`
	PostId    int64 `json:"postId" v:"required" summary:"Post comment id"`
	CommentId int   `json:"commentId" v:"required" summary:"Post comment id"`
}

type DeletePostCommentRes struct{}

type GetPostCommentReq struct {
	g.Meta   `path:"/post/comment/get" method:"get" tags:"Post" summary:"Post get comment"`
	PostId   int64 `json:"postId" summary:"Post comment id"`
	Page     int   `json:"page" summary:"Post comment page"`
	PageSize int   `json:"pageSize" summary:"Post comment page"`
}

type GetPostCommentRes struct {
	List  []*model.PostComment `json:"list"`
	Total int                  `json:"total"`
}
