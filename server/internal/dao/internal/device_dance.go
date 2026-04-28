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

// DeviceDanceDao is the data access object for the table device_dance.
type DeviceDanceDao struct {
	table    string             // table is the underlying table name of the DAO.
	group    string             // group is the database configuration group name of the current DAO.
	columns  DeviceDanceColumns // columns contains all the column names of Table for convenient usage.
	handlers []gdb.ModelHandler // handlers for customized model modification.
}

// DeviceDanceColumns defines and stores column names for the table device_dance.
type DeviceDanceColumns struct {
	Id        string //
	Mac       string // Device MAC address
	DanceName string // Dance name
	DanceData string // MotionData
	MusicUrl  string // Dance background music URL
	CreatedAt string //
	UpdatedAt string //
}

// deviceDanceColumns holds the columns for the table device_dance.
var deviceDanceColumns = DeviceDanceColumns{
	Id:        "id",
	Mac:       "mac",
	DanceName: "dance_name",
	DanceData: "dance_data",
	MusicUrl:  "music_url",
	CreatedAt: "created_at",
	UpdatedAt: "updated_at",
}

// NewDeviceDanceDao creates and returns a new DAO object for table data access.
func NewDeviceDanceDao(handlers ...gdb.ModelHandler) *DeviceDanceDao {
	return &DeviceDanceDao{
		group:    "default",
		table:    "device_dance",
		columns:  deviceDanceColumns,
		handlers: handlers,
	}
}

// DB retrieves and returns the underlying raw database management object of the current DAO.
func (dao *DeviceDanceDao) DB() gdb.DB {
	return g.DB(dao.group)
}

// Table returns the table name of the current DAO.
func (dao *DeviceDanceDao) Table() string {
	return dao.table
}

// Columns returns all column names of the current DAO.
func (dao *DeviceDanceDao) Columns() DeviceDanceColumns {
	return dao.columns
}

// Group returns the database configuration group name of the current DAO.
func (dao *DeviceDanceDao) Group() string {
	return dao.group
}

// Ctx creates and returns a Model for the current DAO. It automatically sets the context for the current operation.
func (dao *DeviceDanceDao) Ctx(ctx context.Context) *gdb.Model {
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
func (dao *DeviceDanceDao) Transaction(ctx context.Context, f func(ctx context.Context, tx gdb.TX) error) (err error) {
	return dao.Ctx(ctx).Transaction(ctx, f)
}
