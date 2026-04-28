/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

// =================================================================================
// Code generated and maintained by GoFrame CLI tool. DO NOT EDIT.
// =================================================================================

package post

import (
	"context"

	"stackChan/api/post/v1"
)

type IPostV1 interface {
	CreatePost(ctx context.Context, req *v1.CreatePostReq) (res *v1.CreatePostRes, err error)
	GetPost(ctx context.Context, req *v1.GetPostReq) (res *v1.GetPostRes, err error)
	DeletePost(ctx context.Context, req *v1.DeletePostReq) (res *v1.DeletePostRes, err error)
	CreatePostComment(ctx context.Context, req *v1.CreatePostCommentReq) (res *v1.CreatePostCommentRes, err error)
	DeletePostComment(ctx context.Context, req *v1.DeletePostCommentReq) (res *v1.DeletePostCommentRes, err error)
	GetPostComment(ctx context.Context, req *v1.GetPostCommentReq) (res *v1.GetPostCommentRes, err error)
}
