/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "assets.h"
#include <assets.h>
#include <mooncake_log.h>
#include <map>
#include <string>
// #include <cstring>

static const std::string_view _tag = "Assets";

namespace assets {

static bool has_suffix(std::string_view str, std::string_view suffix)
{
    return str.size() >= suffix.size() && str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
    return false;
}

lv_image_dsc_t get_image(std::string_view name)
{
    std::string key(name);
    lv_image_dsc_t dsc = {0};

    // 1. Retrieve data from Assets partition
    void* data_ptr   = nullptr;
    size_t data_size = 0;

    // Attempt to access singleton from xiaozhi-esp32/main/assets.h
    // Since we include <assets.h>, we expect class Assets to be available.
    if (!Assets::GetInstance().GetAssetData(key, data_ptr, data_size)) {
        mclog::tagError(_tag, "get image asset {} failed: not found", name);
        return dsc;
    }

    // 2. Construct the LVGL image descriptor
    if (has_suffix(name, ".bin")) {
        // Pre-converted binary (RGB565, etc.) with 4-byte header
        if (data_size > sizeof(lv_image_header_t)) {
            memcpy(&dsc.header, data_ptr, sizeof(lv_image_header_t));
            dsc.data_size = data_size - sizeof(lv_image_header_t);
            dsc.data      = (const uint8_t*)data_ptr + sizeof(lv_image_header_t);
        } else {
            mclog::tagError(_tag, "bin asset {} size too small", name);
        }
    } else if (has_suffix(name, ".png") || has_suffix(name, ".jpg") || has_suffix(name, ".jpeg") ||
               has_suffix(name, ".gif")) {
        // Encoded standard image
        dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
        dsc.header.cf    = LV_COLOR_FORMAT_RAW_ALPHA;
        dsc.data_size    = data_size;
        dsc.data         = (const uint8_t*)data_ptr;
    } else {
        // Fallback for unknown
        mclog::tagWarn(_tag, "unknown asset type for {}, treating as raw", name);
        dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
        dsc.header.cf    = LV_COLOR_FORMAT_RAW;
        dsc.data_size    = data_size;
        dsc.data         = (const uint8_t*)data_ptr;
    }

    return dsc;
}

}  // namespace assets
