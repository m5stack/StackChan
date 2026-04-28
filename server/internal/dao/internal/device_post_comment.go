/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

// ==========================================================================
// Code generated and maintained by GoFrame CLI tool. DO NOT EDIT.
// ==========================================================================

package internal

import (
	"context"

	"github.com/gogf/gf/v2/database/gdb"
	"github.com/gogf/gf/v2/frame/g"
)

// DevicePostCommentDao is the data access object for the table device_post_comment.
type DevicePostCommentDao struct {
	table    string                   // table is the underlying table name of the DAO.
	group    string                   // group is the database configuration group name of the current DAO.
	columns  DevicePostCommentColumns // columns contains all the column names of Table for convenient usage.
	handlers []gdb.ModelHandler       // handlers for customized model modification.
}

// DevicePostCommentColumns defines and stores column names for the table device_post_comment.
type DevicePostCommentColumns struct {
	Id        string //
	PostId    string // Post ID
	Mac       string // Comment device MAC
	Content   string //
	CreatedAt string // Comment time
}

// devicePostCommentColumns holds the columns for the table device_post_comment.
var devicePostCommentColumns = DevicePostCommentColumns{
	Id:        "id",
	PostId:    "post_id",
	Mac:       "mac",
	Content:   "content",
	CreatedAt: "created_at",
}

// NewDevicePostCommentDao creates and returns a new DAO object for table data access.
func NewDevicePostCommentDao(handlers ...gdb.ModelHandler) *DevicePostCommentDao {
	return &DevicePostCommentDao{
		group:    "default",
		table:    "device_post_comment",
		columns:  devicePostCommentColumns,
		handlers: handlers,
	}
}

// DB retrieves and returns the underlying raw database management object of the current DAO.
func (dao *DevicePostCommentDao) DB() gdb.DB {
	return g.DB(dao.group)
}

// Table returns the table name of the current DAO.
func (dao *DevicePostCommentDao) Table() string {
	return dao.table
}

// Columns returns all column names of the current DAO.
func (dao *DevicePostCommentDao) Columns() DevicePostCommentColumns {
	return dao.columns
}

// Group returns the database configuration group name of the current DAO.
func (dao *DevicePostCommentDao) Group() string {
	return dao.group
}

// Ctx creates and returns a Model for the current DAO. It automatically sets the context for the current operation.
func (dao *DevicePostCommentDao) Ctx(ctx context.Context) *gdb.Model {
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
func (dao *DevicePostCommentDao) Transaction(ctx context.Context, f func(ctx context.Context, tx gdb.TX) error) (err error) {
	return dao.Ctx(ctx).Transaction(ctx, f)
}
