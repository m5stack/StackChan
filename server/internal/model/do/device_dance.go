// =================================================================================
// Code generated and maintained by GoFrame CLI tool. DO NOT EDIT.
// =================================================================================

package do

import (
	"github.com/gogf/gf/v2/frame/g"
	"github.com/gogf/gf/v2/os/gtime"
)

// DeviceDance is the golang structure of table device_dance for DAO operations like Where/Data.
type DeviceDance struct {
	g.Meta     `orm:"table:device_dance, do:true"`
	Id         any         //
	Mac        any         // 设备MAC地址
	DanceIndex any         // 舞蹈编号，初始为1~3，可扩展
	DanceData  any         // MotionData
	CreatedAt  *gtime.Time //
	UpdatedAt  *gtime.Time //
}
