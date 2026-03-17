/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void start_ota_update(const char* url, void (*on_progress)(int progress));

#ifdef __cplusplus
}
#endif
