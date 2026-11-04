/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

package service

import (
	"context"
	"stackChan/internal/dao"
	"stackChan/internal/model"
	"stackChan/internal/model/do"

	"github.com/gogf/gf/v2/database/gdb"
	"github.com/gogf/gf/v2/frame/g"
)

const defaultDanceName = "StackChan Dance"

func GetOrCreateDanceList(ctx context.Context, mac string, defaultMusicURL string) (danceList []model.Dance, err error) {
	err = g.DB().Transaction(ctx, func(ctx context.Context, tx gdb.TX) error {
		if err = CreateMacIfNotExistsWithTx(ctx, tx, mac); err != nil {
			return err
		}

		// Serialize default initialization for the same device to avoid duplicate seed rows.
		if _, err = dao.Device.Ctx(ctx).TX(tx).Where("mac", mac).LockUpdate().One(); err != nil {
			return err
		}

		if err = dao.DeviceDance.Ctx(ctx).TX(tx).Where(do.DeviceDance{
			Mac: mac,
		}).Scan(&danceList); err != nil {
			return err
		}

		if len(danceList) > 0 {
			return nil
		}

		if _, err = dao.DeviceDance.Ctx(ctx).TX(tx).Data(do.DeviceDance{
			DanceName: defaultDanceName,
			Mac:       mac,
			MusicUrl:  defaultMusicURL,
			DanceData: model.DefaultDanceData,
		}).Insert(); err != nil {
			return err
		}

		return dao.DeviceDance.Ctx(ctx).TX(tx).Where(do.DeviceDance{
			Mac: mac,
		}).Scan(&danceList)
	})
	return danceList, err
}
