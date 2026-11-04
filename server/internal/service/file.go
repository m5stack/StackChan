/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

package service

import (
	"context"
	"io"
	"os"
	v1 "stackChan/api/file/v1"

	"github.com/gogf/gf/v2/errors/gcode"
	"github.com/gogf/gf/v2/errors/gerror"
)

func AddFile(ctx context.Context, req *v1.FileReq) (res *v1.FileRes, err error) {
	if req.File == nil || req.File.Size == 0 || req.Name == "" {
		return nil, gerror.NewCode(gcode.CodeInvalidParameter, "file or filename is empty")
	}

	fileDir, filePath, returnPath, err := buildUploadFilePath(req.Directory, req.Name)
	if err != nil {
		return nil, err
	}

	currentDir, err := os.Getwd()
	if err != nil {
		return nil, gerror.Wrap(err, "failed to determine upload root")
	}
	uploadRoot, err := openUploadRoot(currentDir)
	if err != nil {
		return nil, err
	}
	defer uploadRoot.Close()

	if fileDir != "." {
		if err := uploadRoot.MkdirAll(fileDir, 0o755); err != nil {
			return nil, gerror.Wrap(err, "failed to create upload directory")
		}
	}

	source, err := req.File.Open()
	if err != nil {
		return nil, gerror.Wrap(err, "failed to open uploaded file")
	}
	defer source.Close()

	target, err := uploadRoot.OpenFile(filePath, os.O_WRONLY|os.O_CREATE|os.O_TRUNC, 0o644)
	if err != nil {
		return nil, gerror.Wrap(err, "failed to open upload destination")
	}

	_, copyErr := io.Copy(target, source)
	closeErr := target.Close()
	if copyErr != nil {
		return nil, gerror.Wrap(copyErr, "failed to write uploaded file")
	}
	if closeErr != nil {
		return nil, gerror.Wrap(closeErr, "failed to close uploaded file")
	}

	return &v1.FileRes{
		Path: returnPath,
	}, nil
}
