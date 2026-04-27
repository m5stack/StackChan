// =================================================================================
// Code generated and maintained by GoFrame CLI tool. DO NOT EDIT.
// =================================================================================

package xiaozhi

import (
	"context"

	"stackChan/api/xiaozhi/v1"
)

type IXiaozhiV1 interface {
	GetXiaoZhiToken(ctx context.Context, req *v1.GetXiaoZhiTokenReq) (res *v1.GetXiaoZhiTokenRes, err error)
	RefreshToken(ctx context.Context, req *v1.RefreshTokenReq) (res *v1.RefreshTokenRes, err error)
	GetXiaoZhiGenerateLicenseToken(ctx context.Context, req *v1.GetXiaoZhiGenerateLicenseTokenReq) (res *v1.GetXiaoZhiGenerateLicenseTokenRes, err error)
}
