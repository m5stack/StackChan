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

// UserDao is the data access object for the table user.
type UserDao struct {
	table    string             // table is the underlying table name of the DAO.
	group    string             // group is the database configuration group name of the current DAO.
	columns  UserColumns        // columns contains all the column names of Table for convenient usage.
	handlers []gdb.ModelHandler // handlers for customized model modification.
}

// UserColumns defines and stores column names for the table user.
type UserColumns struct {
	Uid            string // User unique UID (remote platform primary key)
	Username       string // Login username
	Userslug       string // User alias
	DisplayName    string // User display name
	IconText       string // User icon text
	IconBgColor    string // Icon background color
	EmailConfirmed string // Email verified, 0-no 1-yes
	JoinDate       string // Registration timestamp (milliseconds)
	LastOnline     string // Last online timestamp (milliseconds)
	UserStatus     string // User online status
	CreateAt       string // Local creation time
	UpdateAt       string // Local update time
	IsDeleted      string // Is deleted, 0-normal 1-deleted
}

// userColumns holds the columns for the table user.
var userColumns = UserColumns{
	Uid:            "uid",
	Username:       "username",
	Userslug:       "userslug",
	DisplayName:    "display_name",
	IconText:       "icon_text",
	IconBgColor:    "icon_bg_color",
	EmailConfirmed: "email_confirmed",
	JoinDate:       "join_date",
	LastOnline:     "last_online",
	UserStatus:     "user_status",
	CreateAt:       "create_at",
	UpdateAt:       "update_at",
	IsDeleted:      "is_deleted",
}

// NewUserDao creates and returns a new DAO object for table data access.
func NewUserDao(handlers ...gdb.ModelHandler) *UserDao {
	return &UserDao{
		group:    "default",
		table:    "user",
		columns:  userColumns,
		handlers: handlers,
	}
}

// DB retrieves and returns the underlying raw database management object of the current DAO.
func (dao *UserDao) DB() gdb.DB {
	return g.DB(dao.group)
}

// Table returns the table name of the current DAO.
func (dao *UserDao) Table() string {
	return dao.table
}

// Columns returns all column names of the current DAO.
func (dao *UserDao) Columns() UserColumns {
	return dao.columns
}

// Group returns the database configuration group name of the current DAO.
func (dao *UserDao) Group() string {
	return dao.group
}

// Ctx creates and returns a Model for the current DAO. It automatically sets the context for the current operation.
func (dao *UserDao) Ctx(ctx context.Context) *gdb.Model {
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
func (dao *UserDao) Transaction(ctx context.Context, f func(ctx context.Context, tx gdb.TX) error) (err error) {
	return dao.Ctx(ctx).Transaction(ctx, f)
}
