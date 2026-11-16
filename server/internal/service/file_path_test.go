/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

package service

import (
	"bytes"
	"context"
	"io"
	"mime/multipart"
	"os"
	"path/filepath"
	v1 "stackChan/api/file/v1"
	"testing"

	"github.com/gogf/gf/v2/net/ghttp"
)

func TestCleanFileRelativePathRejectsTraversal(t *testing.T) {
	cases := []string{
		"../go.mod",
		"music/../../go.mod",
		"%2e%2e/go.mod",
		"music/%2e%2e/%2e%2e/go.mod",
		"..\\go.mod",
		"music\\..\\go.mod",
		"/../go.mod",
	}

	for _, tc := range cases {
		if _, err := cleanFileRelativePath(tc); err == nil {
			t.Fatalf("expected traversal path %q to be rejected", tc)
		}
	}
}

func TestCleanFileRelativePathAllowsNormalPaths(t *testing.T) {
	cases := map[string]string{
		"music/stackchan_music.mp3":  "music/stackchan_music.mp3",
		"/music/stackchan_music.mp3": "music/stackchan_music.mp3",
		"apps//icon.png":             "apps/icon.png",
		"apps/./icon.png":            "apps/icon.png",
	}

	for input, want := range cases {
		got, err := cleanFileRelativePath(input)
		if err != nil {
			t.Fatalf("expected path %q to be allowed: %v", input, err)
		}
		if got != want {
			t.Fatalf("cleanFileRelativePath(%q) = %q, want %q", input, got, want)
		}
	}
}

func TestCleanUploadFileNameRejectsPathSegments(t *testing.T) {
	cases := []string{
		"../icon.png",
		"apps/icon.png",
		"apps\\icon.png",
		"apps%2ficon.png",
	}

	for _, tc := range cases {
		if _, err := cleanUploadFileName(tc); err == nil {
			t.Fatalf("expected upload filename %q to be rejected", tc)
		}
	}
}

func TestAddFileWritesInsideUploadRoot(t *testing.T) {
	tmpDir := t.TempDir()
	t.Chdir(tmpDir)

	res, err := AddFile(context.Background(), &v1.FileReq{
		File:      newTestUploadFile(t, []byte("stackchan")),
		Name:      "icon.png",
		Directory: "apps/icons",
	})
	if err != nil {
		t.Fatalf("expected upload to succeed: %v", err)
	}
	if res.Path != "file/apps/icons/icon.png" {
		t.Fatalf("AddFile returned path %q, want %q", res.Path, "file/apps/icons/icon.png")
	}

	uploadedPath := filepath.Join(tmpDir, "file", "apps", "icons", "icon.png")
	content, err := os.ReadFile(uploadedPath)
	if err != nil {
		t.Fatalf("failed to read uploaded file: %v", err)
	}
	if string(content) != "stackchan" {
		t.Fatalf("uploaded content = %q, want %q", content, "stackchan")
	}

	info, err := os.Stat(uploadedPath)
	if err != nil {
		t.Fatalf("failed to inspect uploaded file: %v", err)
	}
	if info.Mode().Perm()&0o111 != 0 {
		t.Fatalf("uploaded file must not be executable, mode is %v", info.Mode().Perm())
	}
}

func TestAddFileRejectsTraversal(t *testing.T) {
	tmpDir := t.TempDir()
	t.Chdir(tmpDir)

	cases := []struct {
		name      string
		directory string
		fileName  string
	}{
		{name: "parent directory", directory: "../outside", fileName: "pwned.txt"},
		{name: "encoded parent directory", directory: "%2e%2e/outside", fileName: "pwned.txt"},
		{name: "windows separator", directory: "..\\outside", fileName: "pwned.txt"},
		{name: "filename path", directory: "", fileName: "../pwned.txt"},
	}

	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			_, err := AddFile(context.Background(), &v1.FileReq{
				File:      newTestUploadFile(t, []byte("owned")),
				Name:      tc.fileName,
				Directory: tc.directory,
			})
			if err == nil {
				t.Fatal("expected traversal upload to be rejected")
			}
		})
	}

	if _, err := os.Stat(filepath.Join(tmpDir, "pwned.txt")); !os.IsNotExist(err) {
		t.Fatalf("traversal created a file outside upload root: %v", err)
	}
}

func TestAddFileRejectsSymlinkEscape(t *testing.T) {
	tmpDir := t.TempDir()
	t.Chdir(tmpDir)

	if err := os.MkdirAll(filepath.Join("file", "safe"), 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.MkdirAll("outside", 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.Symlink(filepath.Join("..", "..", "outside"), filepath.Join("file", "safe", "escape")); err != nil {
		t.Skipf("symlinks are not available: %v", err)
	}

	_, err := AddFile(context.Background(), &v1.FileReq{
		File:      newTestUploadFile(t, []byte("owned")),
		Name:      "pwned.txt",
		Directory: "safe/escape",
	})
	if err == nil {
		t.Fatal("expected upload through an escaping symlink to be rejected")
	}
	if _, err := os.Stat(filepath.Join("outside", "pwned.txt")); !os.IsNotExist(err) {
		t.Fatalf("symlink escape created a file outside upload root: %v", err)
	}
}

func TestAddFileRejectsSymlinkDestination(t *testing.T) {
	tmpDir := t.TempDir()
	t.Chdir(tmpDir)

	if err := os.MkdirAll("file", 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile("outside.txt", []byte("original"), 0o644); err != nil {
		t.Fatal(err)
	}
	if err := os.Symlink(filepath.Join("..", "outside.txt"), filepath.Join("file", "target.txt")); err != nil {
		t.Skipf("symlinks are not available: %v", err)
	}

	_, err := AddFile(context.Background(), &v1.FileReq{
		File: newTestUploadFile(t, []byte("owned")),
		Name: "target.txt",
	})
	if err == nil {
		t.Fatal("expected upload to an escaping symlink to be rejected")
	}
	content, err := os.ReadFile("outside.txt")
	if err != nil {
		t.Fatal(err)
	}
	if string(content) != "original" {
		t.Fatalf("symlink destination was overwritten with %q", content)
	}
}

func TestAddFileRejectsSymlinkUploadRoot(t *testing.T) {
	tmpDir := t.TempDir()
	t.Chdir(tmpDir)

	outsideDir := t.TempDir()
	if err := os.Symlink(outsideDir, "file"); err != nil {
		t.Skipf("symlinks are not available: %v", err)
	}

	_, err := AddFile(context.Background(), &v1.FileReq{
		File: newTestUploadFile(t, []byte("owned")),
		Name: "pwned.txt",
	})
	if err == nil {
		t.Fatal("expected a symlink upload root to be rejected")
	}
	if _, err := os.Stat(filepath.Join(outsideDir, "pwned.txt")); !os.IsNotExist(err) {
		t.Fatalf("symlink upload root was written through: %v", err)
	}
}

func TestOpenFileForServingStaysInsideFileRoot(t *testing.T) {
	tmpDir := t.TempDir()
	t.Chdir(tmpDir)

	if err := os.MkdirAll(filepath.Join("file", "music"), 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join("file", "music", "song.mp3"), []byte("ok"), 0o644); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile("secret.txt", []byte("secret"), 0o644); err != nil {
		t.Fatal(err)
	}

	file, info, err := OpenFileForServing("music/song.mp3")
	if err != nil {
		t.Fatalf("expected file to open: %v", err)
	}
	defer file.Close()
	if !info.Mode().IsRegular() {
		t.Fatalf("opened path is not a regular file: %v", info.Mode())
	}
	content, err := io.ReadAll(file)
	if err != nil {
		t.Fatalf("failed to read opened file: %v", err)
	}
	if string(content) != "ok" {
		t.Fatalf("opened content = %q, want %q", content, "ok")
	}

	if _, _, err := OpenFileForServing("../secret.txt"); err == nil {
		t.Fatal("expected traversal path to be rejected")
	}
	if _, _, err := OpenFileForServing("music"); err == nil {
		t.Fatal("expected directory path to be rejected")
	}
}

func TestOpenFileForServingRejectsSymlinkEscape(t *testing.T) {
	tmpDir := t.TempDir()
	t.Chdir(tmpDir)

	if err := os.MkdirAll("file", 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile("secret.txt", []byte("secret"), 0o644); err != nil {
		t.Fatal(err)
	}
	if err := os.Symlink("../secret.txt", filepath.Join("file", "secret-link")); err != nil {
		t.Skipf("symlinks are not available: %v", err)
	}

	if _, _, err := OpenFileForServing("secret-link"); err == nil {
		t.Fatal("expected symlink escaping file root to be rejected")
	}
}

func TestOpenFileForServingRejectsSymlinkFileRoot(t *testing.T) {
	tmpDir := t.TempDir()
	t.Chdir(tmpDir)

	outsideDir := t.TempDir()
	if err := os.WriteFile(filepath.Join(outsideDir, "secret.txt"), []byte("secret"), 0o644); err != nil {
		t.Fatal(err)
	}
	if err := os.Symlink(outsideDir, "file"); err != nil {
		t.Skipf("symlinks are not available: %v", err)
	}

	if _, _, err := OpenFileForServing("secret.txt"); err == nil {
		t.Fatal("expected a symlink file root to be rejected")
	}
}

func TestOpenFileForServingKeepsOpenedFileAfterPathReplacement(t *testing.T) {
	tmpDir := t.TempDir()
	t.Chdir(tmpDir)

	if err := os.MkdirAll("file", 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join("file", "asset.txt"), []byte("safe"), 0o644); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile("secret.txt", []byte("secret"), 0o644); err != nil {
		t.Fatal(err)
	}

	file, _, err := OpenFileForServing("asset.txt")
	if err != nil {
		t.Fatalf("expected file to open: %v", err)
	}
	defer file.Close()

	if err := os.Rename(filepath.Join("file", "asset.txt"), filepath.Join("file", "original.txt")); err != nil {
		t.Skipf("replacing an opened path is not available: %v", err)
	}
	if err := os.Symlink(filepath.Join("..", "secret.txt"), filepath.Join("file", "asset.txt")); err != nil {
		t.Skipf("symlinks are not available: %v", err)
	}

	content, err := io.ReadAll(file)
	if err != nil {
		t.Fatalf("failed to read opened file: %v", err)
	}
	if string(content) != "safe" {
		t.Fatalf("opened handle followed path replacement, content = %q", content)
	}
}

func newTestUploadFile(t *testing.T, content []byte) *ghttp.UploadFile {
	t.Helper()

	var body bytes.Buffer
	writer := multipart.NewWriter(&body)
	part, err := writer.CreateFormFile("file", "source.bin")
	if err != nil {
		t.Fatal(err)
	}
	if _, err := io.Copy(part, bytes.NewReader(content)); err != nil {
		t.Fatal(err)
	}
	if err := writer.Close(); err != nil {
		t.Fatal(err)
	}

	reader := multipart.NewReader(bytes.NewReader(body.Bytes()), writer.Boundary())
	form, err := reader.ReadForm(1 << 20)
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() {
		_ = form.RemoveAll()
	})

	return &ghttp.UploadFile{FileHeader: form.File["file"][0]}
}
