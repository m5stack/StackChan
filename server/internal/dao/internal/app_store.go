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

// AppStoreDao is the data access object for the table app_store.
type AppStoreDao struct {
	table    string             // table is the underlying table name of the DAO.
	group    string             // group is the database configuration group name of the current DAO.
	columns  AppStoreColumns    // columns contains all the column names of Table for convenient usage.
	handlers []gdb.ModelHandler // handlers for customized model modification.
}

// AppStoreColumns defines and stores column names for the table app_store.
type AppStoreColumns struct {
	Id          string //
	AppName     string // App name
	AppIconUrl  string // App icon URL
	Description string // App description
	FirmwareUrl string // Firmware / installation package download URL
	CreateAt    string // Creation time
	UpdateAt    string // Update time
	IsDeleted   string // Is deleted, 0 normal 1 deleted
}

// appStoreColumns holds the columns for the table app_store.
var appStoreColumns = AppStoreColumns{
	Id:          "id",
	AppName:     "app_name",
	AppIconUrl:  "app_icon_url",
	Description: "description",
	FirmwareUrl: "firmware_url",
	CreateAt:    "create_at",
	UpdateAt:    "update_at",
	IsDeleted:   "is_deleted",
}

// NewAppStoreDao creates and returns a new DAO object for table data access.
func NewAppStoreDao(handlers ...gdb.ModelHandler) *AppStoreDao {
	return &AppStoreDao{
		group:    "default",
		table:    "app_store",
		columns:  appStoreColumns,
		handlers: handlers,
	}
}

// DB retrieves and returns the underlying raw database management object of the current DAO.
func (dao *AppStoreDao) DB() gdb.DB {
	return g.DB(dao.group)
}

// Table returns the table name of the current DAO.
func (dao *AppStoreDao) Table() string {
	return dao.table
}

// Columns returns all column names of the current DAO.
func (dao *AppStoreDao) Columns() AppStoreColumns {
	return dao.columns
}

// Group returns the database configuration group name of the current DAO.
func (dao *AppStoreDao) Group() string {
	return dao.group
}

// Ctx creates and returns a Model for the current DAO. It automatically sets the context for the current operation.
func (dao *AppStoreDao) Ctx(ctx context.Context) *gdb.Model {
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
func (dao *AppStoreDao) Transaction(ctx context.Context, f func(ctx context.Context, tx gdb.TX) error) (err error) {
	return dao.Ctx(ctx).Transaction(ctx, f)
}
