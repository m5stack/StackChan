#pragma once
#include <cstdint>
#include <cstddef>
#include <memory>
#include <lvgl_image.h>

namespace jpeg_dec {

/**
 * @brief Decodes a JPEG buffer to RGB565 format suitable for LVGL.
 *
 * @param jpeg_data Pointer to the JPEG data.
 * @param jpeg_len Length of the JPEG data.
 * @return std::shared_ptr<LvglAllocatedImage> The decoded image wrapped in LvglAllocatedImage, or nullptr on failure.
 */
std::shared_ptr<LvglAllocatedImage> decode_to_lvgl(const uint8_t* jpeg_data, size_t jpeg_len);

}  // namespace jpeg_dec
