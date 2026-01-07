// =================================================================================
// Code generated and maintained by GoFrame CLI tool. DO NOT EDIT.
// =================================================================================

package entity

import (
	"github.com/gogf/gf/v2/os/gtime"
)

// DevicePost is the golang structure for table device_post.
type DevicePost struct {
	Id           int64       `json:"id"           orm:"id"            description:""`        //
	Mac          string      `json:"mac"          orm:"mac"           description:"发帖设备MAC"` // 发帖设备MAC
	ContentText  string      `json:"contentText"  orm:"content_text"  description:"文本内容"`    // 文本内容
	ContentImage string      `json:"contentImage" orm:"content_image" description:"图片URL"`   // 图片URL
	CreatedAt    *gtime.Time `json:"createdAt"    orm:"created_at"    description:"发帖时间"`    // 发帖时间
}
