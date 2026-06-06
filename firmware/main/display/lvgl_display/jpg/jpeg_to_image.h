#pragma once

#include "sdkconfig.h"

#ifndef CONFIG_IDF_TARGET_ESP32

#include <esp_err.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t jpeg_to_image(const uint8_t* src, size_t src_len, uint8_t** out, size_t* out_len, size_t* width,
                        size_t* height, size_t* stride);

#ifdef __cplusplus
}
#endif

#endif  // CONFIG_IDF_TARGET_ESP32
