#include "jpeg_decoder.h"
#include <esp_log.h>
#include <esp_heap_caps.h>
#include <esp_jpeg_dec.h>
#include <cstring>

static const char* TAG = "JpegDecoder";

namespace jpeg_dec {

std::shared_ptr<LvglAllocatedImage> decode_to_lvgl(const uint8_t* jpeg_data, size_t jpeg_len)
{
    if (!jpeg_data || jpeg_len == 0) {
        ESP_LOGE(TAG, "Invalid input data");
        return nullptr;
    }

    jpeg_dec_config_t config = DEFAULT_JPEG_DEC_CONFIG();
    config.output_type       = JPEG_PIXEL_FORMAT_RGB565_LE;

    jpeg_dec_handle_t jpeg_dec = NULL;
    if (jpeg_dec_open(&config, &jpeg_dec) != JPEG_ERR_OK) {
        ESP_LOGE(TAG, "Failed to open JPEG decoder");
        return nullptr;
    }

    // Ensure decoder is closed when function exits
    struct JpegDecCloser {
        jpeg_dec_handle_t handle;
        ~JpegDecCloser()
        {
            if (handle) jpeg_dec_close(handle);
        }
    } closer{jpeg_dec};

    jpeg_dec_io_t io = {0};
    io.inbuf         = (uint8_t*)jpeg_data;
    io.inbuf_len     = jpeg_len;

    jpeg_dec_header_info_t out_info = {0};
    if (jpeg_dec_parse_header(jpeg_dec, &io, &out_info) != JPEG_ERR_OK) {
        ESP_LOGE(TAG, "Failed to parse JPEG header");
        return nullptr;
    }

    int out_size = out_info.width * out_info.height * 2;
    // Allocate memory for the output image (16-byte aligned required by esp_jpeg_dec)
    uint8_t* out_buf = (uint8_t*)heap_caps_aligned_alloc(16, out_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!out_buf) {
        // Fallback to internal memory if SPIRAM allocation fails
        out_buf = (uint8_t*)heap_caps_aligned_alloc(16, out_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }

    if (!out_buf) {
        ESP_LOGE(TAG, "Failed to allocate output buffer");
        return nullptr;
    }

    io.outbuf = out_buf;
    if (jpeg_dec_process(jpeg_dec, &io) != JPEG_ERR_OK) {
        ESP_LOGE(TAG, "Failed to process JPEG");
        heap_caps_free(out_buf);
        return nullptr;
    }

    try {
        // LvglAllocatedImage takes ownership of out_buf and will free it with heap_caps_free
        return std::make_shared<LvglAllocatedImage>(out_buf, out_size, out_info.width, out_info.height,
                                                    out_info.width * 2, LV_COLOR_FORMAT_RGB565);
    } catch (const std::exception& e) {
        ESP_LOGE(TAG, "Failed to create LvglAllocatedImage: %s", e.what());
        heap_caps_free(out_buf);
        return nullptr;
    }
}

}  // namespace jpeg_dec
