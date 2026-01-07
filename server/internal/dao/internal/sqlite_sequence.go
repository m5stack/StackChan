// ==========================================================================
// Code generated and maintained by GoFrame CLI tool. DO NOT EDIT.
// ==========================================================================

package internal

import (
	"context"

	"github.com/gogf/gf/v2/database/gdb"
	"github.com/gogf/gf/v2/frame/g"
)

// SqliteSequenceDao is the data access object for the table sqlite_sequence.
type SqliteSequenceDao struct {
	table    string                // table is the underlying table name of the DAO.
	group    string                // group is the database configuration group name of the current DAO.
	columns  SqliteSequenceColumns // columns contains all the column names of Table for convenient usage.
	handlers []gdb.ModelHandler    // handlers for customized model modification.
}

// SqliteSequenceColumns defines and stores column names for the table sqlite_sequence.
type SqliteSequenceColumns struct {
	Name string //
	Seq  string //
}

// sqliteSequenceColumns holds the columns for the table sqlite_sequence.
var sqliteSequenceColumns = SqliteSequenceColumns{
	Name: "name",
	Seq:  "seq",
}

// NewSqliteSequenceDao creates and returns a new DAO object for table data access.
func NewSqliteSequenceDao(handlers ...gdb.ModelHandler) *SqliteSequenceDao {
	return &SqliteSequenceDao{
		group:    "default",
		table:    "sqlite_sequence",
		columns:  sqliteSequenceColumns,
		handlers: handlers,
	}
}

// DB retrieves and returns the underlying raw database management object of the current DAO.
func (dao *SqliteSequenceDao) DB() gdb.DB {
	return g.DB(dao.group)
}

// Table returns the table name of the current DAO.
func (dao *SqliteSequenceDao) Table() string {
	return dao.table
}

// Columns returns all column names of the current DAO.
func (dao *SqliteSequenceDao) Columns() SqliteSequenceColumns {
	return dao.columns
}

// Group returns the database configuration group name of the current DAO.
func (dao *SqliteSequenceDao) Group() string {
	return dao.group
}

// Ctx creates and returns a Model for the current DAO. It automatically sets the context for the current operation.
func (dao *SqliteSequenceDao) Ctx(ctx context.Context) *gdb.Model {
	model := dao.DB().Model(dao.table)
	for _, handler := range dao.handlers {
		model = handler(model)
	}
	return model.Safe().Ctx(ctx)
}

// Transaction wraps the transaction logic using function f.
// It rolls back the transaction and returns the error if function f returns a non-nil error.
// It commits the transaction and returns nil if function f returns nil.
//
// Note: Do not commit or roll back the transaction in function f,
// as it is automatically handled by this function.
func (dao *SqliteSequenceDao) Transaction(ctx context.Context, f func(ctx context.Context, tx gdb.TX) error) (err error) {
	return dao.Ctx(ctx).Transaction(ctx, f)
}
