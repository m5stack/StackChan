/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

package service

import (
	"context"
	"stackChan/internal/dao"
	"stackChan/internal/model"
)

func GetAppList(ctx context.Context) ([]model.AppInfo, error) {
	var apps = make([]model.AppInfo, 0)
	err := dao.AppStore.
		Ctx(ctx).
		Where("is_deleted", 0).
		Scan(&apps)
	if err != nil {
		return nil, err
	}
	return apps, nil
}
