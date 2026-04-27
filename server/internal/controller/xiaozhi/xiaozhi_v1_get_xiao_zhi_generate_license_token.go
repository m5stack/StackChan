package xiaozhi

import (
	"context"

	"github.com/gogf/gf/v2/errors/gcode"
	"github.com/gogf/gf/v2/errors/gerror"
	"github.com/gogf/gf/v2/frame/g"

	"stackChan/api/xiaozhi/v1"
)

func (c *ControllerV1) GetXiaoZhiGenerateLicenseToken(ctx context.Context, req *v1.GetXiaoZhiGenerateLicenseTokenReq) (res *v1.GetXiaoZhiGenerateLicenseTokenRes, err error) {
	generateLicenseToken := g.Cfg().MustGet(ctx, "xiaozhi.generate_license_token").String()
	if generateLicenseToken == "" {
		return nil, gerror.NewCode(gcode.CodeInvalidParameter, "generate_license_token is empty")
	}
	return new(v1.GetXiaoZhiGenerateLicenseTokenRes(generateLicenseToken)), nil
}
