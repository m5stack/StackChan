// =================================================================================
// Code generated and maintained by GoFrame CLI tool. DO NOT EDIT.
// =================================================================================

package appstore

import (
	"context"

	"stackChan/api/appstore/v1"
)

type IAppstoreV1 interface {
	GetAppList(ctx context.Context, req *v1.GetAppListReq) (res *v1.GetAppListRes, err error)
}
