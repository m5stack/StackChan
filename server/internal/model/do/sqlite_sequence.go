// =================================================================================
// Code generated and maintained by GoFrame CLI tool. DO NOT EDIT.
// =================================================================================

package do

import (
	"github.com/gogf/gf/v2/frame/g"
)

// SqliteSequence is the golang structure of table sqlite_sequence for DAO operations like Where/Data.
type SqliteSequence struct {
	g.Meta `orm:"table:sqlite_sequence, do:true"`
	Name   any //
	Seq    any //
}
