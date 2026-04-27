// =================================================================================
// Code generated and maintained by GoFrame CLI tool. DO NOT EDIT.
// =================================================================================

package user

import (
	"context"

	"stackChan/api/user/v2"
)

type IUserV2 interface {
	Login(ctx context.Context, req *v2.LoginReq) (res *v2.LoginRes, err error)
	GetUserInfo(ctx context.Context, req *v2.GetUserInfoReq) (res *v2.GetUserInfoRes, err error)
	Registration(ctx context.Context, req *v2.RegistrationReq) (res *v2.RegistrationRes, err error)
}
