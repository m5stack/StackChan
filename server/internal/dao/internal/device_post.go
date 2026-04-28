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

// DevicePostDao is the data access object for the table device_post.
type DevicePostDao struct {
	table    string             // table is the underlying table name of the DAO.
	group    string             // group is the database configuration group name of the current DAO.
	columns  DevicePostColumns  // columns contains all the column names of Table for convenient usage.
	handlers []gdb.ModelHandler // handlers for customized model modification.
}

// DevicePostColumns defines and stores column names for the table device_post.
type DevicePostColumns struct {
	Id           string //
	Mac          string // Post device MAC
	ContentText  string //
	ContentImage string // Image URL
	CreatedAt    string // Post time
}

// devicePostColumns holds the columns for the table device_post.
var devicePostColumns = DevicePostColumns{
	Id:           "id",
	Mac:          "mac",
	ContentText:  "content_text",
	ContentImage: "content_image",
	CreatedAt:    "created_at",
}

// NewDevicePostDao creates and returns a new DAO object for table data access.
func NewDevicePostDao(handlers ...gdb.ModelHandler) *DevicePostDao {
	return &DevicePostDao{
		group:    "default",
		table:    "device_post",
		columns:  devicePostColumns,
		handlers: handlers,
	}
}

// DB retrieves and returns the underlying raw database management object of the current DAO.
func (dao *DevicePostDao) DB() gdb.DB {
	return g.DB(dao.group)
}

// Table returns the table name of the current DAO.
func (dao *DevicePostDao) Table() string {
	return dao.table
}

// Columns returns all column names of the current DAO.
func (dao *DevicePostDao) Columns() DevicePostColumns {
	return dao.columns
}

// Group returns the database configuration group name of the current DAO.
func (dao *DevicePostDao) Group() string {
	return dao.group
}

// Ctx creates and returns a Model for the current DAO. It automatically sets the context for the current operation.
func (dao *DevicePostDao) Ctx(ctx context.Context) *gdb.Model {
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
func (dao *DevicePostDao) Transaction(ctx context.Context, f func(ctx context.Context, tx gdb.TX) error) (err error) {
	return dao.Ctx(ctx).Transaction(ctx, f)
}
