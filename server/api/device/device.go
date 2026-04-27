// =================================================================================
// Code generated and maintained by GoFrame CLI tool. DO NOT EDIT.
// =================================================================================

package device

import (
	"context"

	"stackChan/api/device/v1"
	"stackChan/api/device/v2"
)

type IDeviceV1 interface {
	Create(ctx context.Context, req *v1.CreateReq) (res *v1.CreateRes, err error)
	Update(ctx context.Context, req *v1.UpdateReq) (res *v1.UpdateRes, err error)
	GetRandomDevice(ctx context.Context, req *v1.GetRandomDeviceReq) (res *v1.GetRandomDeviceRes, err error)
	GetDeviceInfo(ctx context.Context, req *v1.GetDeviceInfoReq) (res *v1.GetDeviceInfoRes, err error)
	UpdateDeviceInfo(ctx context.Context, req *v1.UpdateDeviceInfoReq) (res *v1.UpdateDeviceInfoRes, err error)
}

type IDeviceV2 interface {
	GetDevices(ctx context.Context, req *v2.GetDevicesReq) (res *v2.GetDevicesRes, err error)
	BindDevice(ctx context.Context, req *v2.BindDeviceReq) (res *v2.BindDeviceRes, err error)
	UnbindDevice(ctx context.Context, req *v2.UnbindDeviceReq) (res *v2.UnbindDeviceRes, err error)
	UpdateDevice(ctx context.Context, req *v2.UpdateDeviceReq) (res *v2.UpdateDeviceRes, err error)
	AgentRestoreDefault(ctx context.Context, req *v2.AgentRestoreDefaultReq) (res *v2.AgentRestoreDefaultRes, err error)
}
