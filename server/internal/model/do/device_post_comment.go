// =================================================================================
// Code generated and maintained by GoFrame CLI tool. DO NOT EDIT.
// =================================================================================

package do

import (
	"github.com/gogf/gf/v2/frame/g"
	"github.com/gogf/gf/v2/os/gtime"
)

// DevicePostComment is the golang structure of table device_post_comment for DAO operations like Where/Data.
type DevicePostComment struct {
	g.Meta    `orm:"table:device_post_comment, do:true"`
	Id        any         //
	PostId    any         // 帖子ID
	Mac       any         // 评论设备MAC
	Content   any         // 评论内容
	CreatedAt *gtime.Time // 评论时间
}
