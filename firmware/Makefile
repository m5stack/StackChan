SHELL := /bin/bash

PROJECT_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
REPO_DIR := $(shell git -C "$(PROJECT_DIR)" rev-parse --show-toplevel 2>/dev/null || echo "$(PROJECT_DIR)")
MAIN_WORKTREE_DIR := $(shell common_git_dir=$$(git -C "$(PROJECT_DIR)" rev-parse --path-format=absolute --git-common-dir 2>/dev/null); \
	if [ -n "$$common_git_dir" ]; then dirname "$$common_git_dir"; else echo "$(REPO_DIR)"; fi)
ESP_IDF_ROOT ?= $(if $(IDF_PATH),$(IDF_PATH),$(shell for dir in "$(PROJECT_DIR)/../esp-idf" "$(REPO_DIR)/esp-idf" "$(MAIN_WORKTREE_DIR)/esp-idf"; do \
	if [ -f "$$dir/export.sh" ]; then echo "$$dir"; exit; fi; done; echo "$(PROJECT_DIR)/../esp-idf"))
PORT ?= /dev/ttyACM0
DEPS_STAMP := $(PROJECT_DIR)/.deps/submodules.stamp
IDF_CCACHE_ENABLE ?= $(shell command -v ccache >/dev/null 2>&1 && echo 1)
export IDF_CCACHE_ENABLE

.PHONY: deps configure build flash fullflash

deps:
	@mkdir -p "$(dir $(DEPS_STAMP))"
	@git -C "$(REPO_DIR)" submodule update --init --recursive
	@git -C "$(REPO_DIR)" submodule status --recursive > "$(DEPS_STAMP)"

$(DEPS_STAMP): $(REPO_DIR)/.gitmodules
	@$(MAKE) --no-print-directory -C "$(PROJECT_DIR)" deps

configure build flash fullflash: $(DEPS_STAMP)
	@idf_root="$(ESP_IDF_ROOT)"; \
		test -n "$$idf_root" || { echo "ESP-IDF not found. Set ESP_IDF_ROOT=/path/to/esp-idf or place it at ../esp-idf."; exit 1; }; \
		test -f "$$idf_root/export.sh" || { echo "Missing ESP-IDF export script: $$idf_root/export.sh"; exit 1; }; \
		cd "$(PROJECT_DIR)" && \
		source "$$idf_root/export.sh" >/dev/null 2>&1 && \
		idf.py $(if $(filter configure,$@),reconfigure,$(if $(filter build,$@),build,-p "$(PORT)" $(if $(filter flash,$@),app-flash,flash)))
