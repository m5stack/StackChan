// =================================================================================
// Code generated and maintained by GoFrame CLI tool. DO NOT EDIT.
// =================================================================================

package entity

import (
	"github.com/gogf/gf/v2/os/gtime"
)

// DeviceDance is the golang structure for table device_dance.
type DeviceDance struct {
	Id         int64       `json:"id"         orm:"id"          description:""`                //
	Mac        string      `json:"mac"        orm:"mac"         description:"设备MAC地址"`         // 设备MAC地址
	DanceIndex int         `json:"danceIndex" orm:"dance_index" description:"舞蹈编号，初始为1~3，可扩展"` // 舞蹈编号，初始为1~3，可扩展
	DanceData  string      `json:"danceData"  orm:"dance_data"  description:"MotionData"`      // MotionData
	CreatedAt  *gtime.Time `json:"createdAt"  orm:"created_at"  description:""`                //
	UpdatedAt  *gtime.Time `json:"updatedAt"  orm:"updated_at"  description:""`                //
}
