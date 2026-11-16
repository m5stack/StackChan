/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

package service

import (
	"net/url"
	"os"
	"path"
	"path/filepath"
	"strings"

	"github.com/gogf/gf/v2/errors/gcode"
	"github.com/gogf/gf/v2/errors/gerror"
)

const fileBaseDir = "file"

func OpenFileForServing(requestPath string) (*os.File, os.FileInfo, error) {
	relativePath, err := cleanFileRelativePath(requestPath)
	if err != nil {
		return nil, nil, err
	}

	currentDir, err := os.Getwd()
	if err != nil {
		return nil, nil, gerror.Wrap(err, "failed to determine file root")
	}
	root, err := openFileRoot(currentDir)
	if err != nil {
		return nil, nil, err
	}
	defer root.Close()

	file, err := root.Open(filepath.FromSlash(relativePath))
	if err != nil {
		return nil, nil, gerror.Wrap(err, "failed to open served file")
	}

	info, err := file.Stat()
	if err != nil {
		_ = file.Close()
		return nil, nil, gerror.Wrap(err, "failed to inspect served file")
	}
	if !info.Mode().IsRegular() {
		_ = file.Close()
		return nil, nil, invalidFilePathError()
	}

	return file, info, nil
}

func buildUploadFilePath(directory string, name string) (fileDir string, filePath string, returnPath string, err error) {
	fileDir = "."

	returnPathParts := []string{fileBaseDir}
	if directory != "" {
		cleanDirectory, err := cleanFileRelativePath(directory)
		if err != nil {
			return "", "", "", err
		}
		fileDir = filepath.FromSlash(cleanDirectory)
		returnPathParts = append(returnPathParts, strings.Split(cleanDirectory, "/")...)
	}

	cleanName, err := cleanUploadFileName(name)
	if err != nil {
		return "", "", "", err
	}

	filePath = filepath.Join(fileDir, cleanName)
	returnPathParts = append(returnPathParts, cleanName)
	returnPath = path.Join(returnPathParts...)
	return fileDir, filePath, returnPath, nil
}

func fileRootPath(currentDir string) string {
	return filepath.Join(currentDir, fileBaseDir)
}

func openUploadRoot(currentDir string) (*os.Root, error) {
	basePath := fileRootPath(currentDir)
	if err := os.MkdirAll(basePath, 0o755); err != nil {
		return nil, gerror.Wrap(err, "failed to create upload root")
	}
	return openFileRoot(currentDir)
}

func openFileRoot(currentDir string) (*os.Root, error) {
	basePath := fileRootPath(currentDir)
	root, err := os.OpenRoot(basePath)
	if err != nil {
		return nil, gerror.Wrap(err, "failed to open file root")
	}

	// Pin the real directory and reject a symlink or replacement at the root path.
	openedInfo, err := root.Stat(".")
	if err != nil {
		_ = root.Close()
		return nil, gerror.Wrap(err, "failed to inspect file root")
	}
	pathInfo, err := os.Lstat(basePath)
	if err != nil {
		_ = root.Close()
		return nil, gerror.Wrap(err, "failed to inspect file root path")
	}
	if pathInfo.Mode()&os.ModeSymlink != 0 || !pathInfo.IsDir() || !os.SameFile(openedInfo, pathInfo) {
		_ = root.Close()
		return nil, invalidFilePathError()
	}

	return root, nil
}

func cleanUploadFileName(name string) (string, error) {
	cleanName, err := cleanFileRelativePath(name)
	if err != nil {
		return "", err
	}
	if cleanName != path.Base(cleanName) {
		return "", invalidFilePathError()
	}
	return cleanName, nil
}

func cleanFileRelativePath(rawPath string) (string, error) {
	if rawPath == "" {
		return "", invalidFilePathError()
	}

	normalizedPath := strings.ReplaceAll(rawPath, "\\", "/")
	if decodedPath, err := url.PathUnescape(normalizedPath); err == nil {
		normalizedPath = strings.ReplaceAll(decodedPath, "\\", "/")
	}
	normalizedPath = strings.TrimLeft(normalizedPath, "/")
	if normalizedPath == "" {
		return "", invalidFilePathError()
	}

	for _, segment := range strings.Split(normalizedPath, "/") {
		if segment == ".." {
			return "", invalidFilePathError()
		}
	}

	cleanPath := path.Clean(normalizedPath)
	if cleanPath == "." || cleanPath == ".." || strings.HasPrefix(cleanPath, "../") {
		return "", invalidFilePathError()
	}
	filepathCleanPath := filepath.FromSlash(cleanPath)
	if filepath.IsAbs(filepathCleanPath) || filepath.VolumeName(filepathCleanPath) != "" {
		return "", invalidFilePathError()
	}

	return cleanPath, nil
}

func invalidFilePathError() error {
	return gerror.NewCode(gcode.CodeInvalidParameter, "invalid file path")
}
