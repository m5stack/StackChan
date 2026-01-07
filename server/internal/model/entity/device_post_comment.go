// =================================================================================
// Code generated and maintained by GoFrame CLI tool. DO NOT EDIT.
// =================================================================================

package entity

import (
	"github.com/gogf/gf/v2/os/gtime"
)

// DevicePostComment is the golang structure for table device_post_comment.
type DevicePostComment struct {
	Id        int64       `json:"id"        orm:"id"         description:""`        //
	PostId    int64       `json:"postId"    orm:"post_id"    description:"帖子ID"`    // 帖子ID
	Mac       string      `json:"mac"       orm:"mac"        description:"评论设备MAC"` // 评论设备MAC
	Content   string      `json:"content"   orm:"content"    description:"评论内容"`    // 评论内容
	CreatedAt *gtime.Time `json:"createdAt" orm:"created_at" description:"评论时间"`    // 评论时间
}
