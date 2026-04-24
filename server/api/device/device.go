// =================================================================================
// Code generated and maintained by GoFrame CLI tool. DO NOT EDIT.
// =================================================================================

package device

import (
	"context"

	"stackChan/api/device/v1"
)

type IDeviceV1 interface {
	Create(ctx context.Context, req *v1.CreateReq) (res *v1.CreateRes, err error)
	Update(ctx context.Context, req *v1.UpdateReq) (res *v1.UpdateRes, err error)
	GetRandomDevice(ctx context.Context, req *v1.GetRandomDeviceReq) (res *v1.GetRandomDeviceRes, err error)
	GetDeviceInfo(ctx context.Context, req *v1.GetDeviceInfoReq) (res *v1.GetDeviceInfoRes, err error)
	UpdateDeviceInfo(ctx context.Context, req *v1.UpdateDeviceInfoReq) (res *v1.UpdateDeviceInfoRes, err error)
	GetUserAccountInfo(ctx context.Context, req *v1.GetUserAccountInfoReq) (res *v1.GetUserAccountInfoRes, err error)
	UnbindAccount(ctx context.Context, req *v1.UnbindAccountReq) (res *v1.UnbindAccountRes, err error)
}
