/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

package service

import (
	"context"
	"io"
	"os"
	"path/filepath"
	v1 "stackChan/api/file/v1"

	"github.com/gogf/gf/v2/errors/gcode"
	"github.com/gogf/gf/v2/errors/gerror"
)

func AddFile(ctx context.Context, req *v1.FileReq) (res *v1.FileRes, err error) {
	currentDir, err := os.Getwd()
	if err != nil {
		return nil, err
	}

	baseDir := "file"
	fileDir := filepath.Join(currentDir, baseDir)

	if req.Directory != "" {
		fileDir = filepath.Join(fileDir, req.Directory)
	}

	if _, err := os.Stat(fileDir); os.IsNotExist(err) {
		if err := os.MkdirAll(fileDir, os.ModePerm); err != nil {
			return nil, err
		}
	}

	if req.File.Size == 0 || req.Name == "" {
		return nil, gerror.NewCode(gcode.CodeInvalidParameter, "file or filename is empty")
	}

	filePath := filepath.Join(fileDir, req.Name)

	file, err := req.File.Open()
	if err != nil {
		return nil, err
	}

	fileBytes, err := io.ReadAll(file)
	if err != nil {
		return nil, err
	}

	if err := os.WriteFile(filePath, fileBytes, os.ModePerm); err != nil {
		return nil, err
	}

	_ = file.Close()

	var returnPath string
	if req.Directory != "" {
		returnPath = filepath.Join(baseDir, req.Directory, req.Name)
	} else {
		returnPath = filepath.Join(baseDir, req.Name)
	}

	return &v1.FileRes{
		Path: returnPath,
	}, nil
}
