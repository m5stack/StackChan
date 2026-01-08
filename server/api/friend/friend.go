// =================================================================================
// Code generated and maintained by GoFrame CLI tool. DO NOT EDIT.
// =================================================================================

package friend

import (
	"context"

	"stackChan/api/friend/v1"
)

type IFriendV1 interface {
	Add(ctx context.Context, req *v1.AddReq) (res *v1.AddRes, err error)
}
