/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

package service

import (
	"context"
	"stackChan/internal/dao"
	"stackChan/internal/model/do"
	"stackChan/internal/model/entity"
)

func CreateMacIfNotExists(ctx context.Context, mac string) (id int64, err error) {
	count, err := dao.Device.Ctx(ctx).Where("mac = ?", mac).Count()
	if err != nil {
		return 0, err
	}
	if count > 0 {
		return 0, nil
	}
	id, err = dao.Device.Ctx(ctx).Data(do.Device{
		Mac: mac,
	}).InsertAndGetId()

	if err != nil {
		return 0, err
	}
	return id, nil
}

func GetDeviceName(ctx context.Context, mac string) (name string, err error) {
	var device entity.Device
	err = dao.Device.Ctx(ctx).Where("mac = ?", mac).Fields("name").Scan(&device)
	if err != nil {
		return "", err
	}
	return device.Name, nil
}
