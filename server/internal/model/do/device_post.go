// =================================================================================
// Code generated and maintained by GoFrame CLI tool. DO NOT EDIT.
// =================================================================================

package do

import (
	"github.com/gogf/gf/v2/frame/g"
	"github.com/gogf/gf/v2/os/gtime"
)

// DevicePost is the golang structure of table device_post for DAO operations like Where/Data.
type DevicePost struct {
	g.Meta       `orm:"table:device_post, do:true"`
	Id           any         //
	Mac          any         // 发帖设备MAC
	ContentText  any         // 文本内容
	ContentImage any         // 图片URL
	CreatedAt    *gtime.Time // 发帖时间
}
