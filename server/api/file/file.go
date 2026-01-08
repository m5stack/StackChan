// =================================================================================
// Code generated and maintained by GoFrame CLI tool. DO NOT EDIT.
// =================================================================================

package file

import (
	"context"

	"stackChan/api/file/v1"
)

type IFileV1 interface {
	File(ctx context.Context, req *v1.FileReq) (res *v1.FileRes, err error)
}
