// =================================================================================
// Code generated and maintained by GoFrame CLI tool. DO NOT EDIT.
// =================================================================================

package dance

import (
	"context"

	"stackChan/api/dance/v1"
	"stackChan/api/dance/v2"
)

type IDanceV1 interface {
	Create(ctx context.Context, req *v1.CreateReq) (res *v1.CreateRes, err error)
	Delete(ctx context.Context, req *v1.DeleteReq) (res *v1.DeleteRes, err error)
	Update(ctx context.Context, req *v1.UpdateReq) (res *v1.UpdateRes, err error)
	GetList(ctx context.Context, req *v1.GetListReq) (res *v1.GetListRes, err error)
	GetDanceInfo(ctx context.Context, req *v1.GetDanceInfoReq) (res *v1.GetDanceInfoRes, err error)
	GetMusicList(ctx context.Context, req *v1.GetMusicListReq) (res *v1.GetMusicListRes, err error)
}

type IDanceV2 interface {
	GetList(ctx context.Context, req *v2.GetListReq) (res *v2.GetListRes, err error)
	Create(ctx context.Context, req *v2.CreateReq) (res *v2.CreateRes, err error)
	Delete(ctx context.Context, req *v2.DeleteReq) (res *v2.DeleteRes, err error)
	Update(ctx context.Context, req *v2.UpdateReq) (res *v2.UpdateRes, err error)
	GetDanceInfo(ctx context.Context, req *v2.GetDanceInfoReq) (res *v2.GetDanceInfoRes, err error)
}
