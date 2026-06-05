SHELL := /bin/bash

PROJECT_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
ESP_IDF_ROOT ?= $(PROJECT_DIR)/../esp-idf
PORT ?= /dev/ttyACM0

.PHONY: build flash fullflash

build flash fullflash:
	@idf_root="$(ESP_IDF_ROOT)"; \
		test -n "$$idf_root" || { echo "ESP-IDF not found. Set ESP_IDF_ROOT=/path/to/esp-idf or place it at ../esp-idf."; exit 1; }; \
		test -f "$$idf_root/export.sh" || { echo "Missing ESP-IDF export script: $$idf_root/export.sh"; exit 1; }; \
		cd "$(PROJECT_DIR)" && \
		source "$$idf_root/export.sh" >/dev/null 2>&1 && \
		idf.py $(if $(filter build,$@),build,-p "$(PORT)" $(if $(filter flash,$@),app-flash,flash))
