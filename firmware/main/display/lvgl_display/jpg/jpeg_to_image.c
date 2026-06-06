#include "jpeg_to_image.h"

#ifndef CONFIG_IDF_TARGET_ESP32

#include <esp_heap_caps.h>
#include <esp_jpeg_dec.h>
#include <esp_log.h>

#include <string.h>

static const char* TAG = "JpegDecode";

static void clear_outputs(uint8_t** out, size_t* out_len, size_t* width, size_t* height, size_t* stride)
{
    if (out != NULL) {
        *out = NULL;
    }
    if (out_len != NULL) {
        *out_len = 0;
    }
    if (width != NULL) {
        *width = 0;
    }
    if (height != NULL) {
        *height = 0;
    }
    if (stride != NULL) {
        *stride = 0;
    }
}

static esp_err_t jpeg_error_to_esp(jpeg_error_t error)
{
    switch (error) {
        case JPEG_ERR_OK:
            return ESP_OK;
        case JPEG_ERR_NO_MEM:
            return ESP_ERR_NO_MEM;
        case JPEG_ERR_INVALID_PARAM:
        case JPEG_ERR_BAD_DATA:
        case JPEG_ERR_NO_MORE_DATA:
            return ESP_ERR_INVALID_ARG;
        case JPEG_ERR_UNSUPPORT_FMT:
        case JPEG_ERR_UNSUPPORT_STD:
            return ESP_ERR_NOT_SUPPORTED;
        case JPEG_ERR_FAIL:
        default:
            return ESP_FAIL;
    }
}

esp_err_t jpeg_to_image(const uint8_t* src, size_t src_len, uint8_t** out, size_t* out_len, size_t* width,
                        size_t* height, size_t* stride)
{
    clear_outputs(out, out_len, width, height, stride);
    if (src == NULL || src_len == 0 || out == NULL || out_len == NULL || width == NULL || height == NULL ||
        stride == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    jpeg_dec_config_t config = DEFAULT_JPEG_DEC_CONFIG();
    config.output_type = JPEG_PIXEL_FORMAT_RGB565_LE;
    config.rotate = JPEG_ROTATE_0D;

    jpeg_dec_handle_t decoder = NULL;
    jpeg_error_t jpeg_ret = jpeg_dec_open(&config, &decoder);
    if (jpeg_ret != JPEG_ERR_OK) {
        return jpeg_error_to_esp(jpeg_ret);
    }

    jpeg_dec_io_t io = {
        .inbuf = (uint8_t*)src,
        .inbuf_len = (int)src_len,
        .inbuf_remain = 0,
        .outbuf = NULL,
        .out_size = 0,
    };

    jpeg_dec_header_info_t info = {0};
    esp_err_t result = ESP_OK;
    jpeg_ret = jpeg_dec_parse_header(decoder, &io, &info);
    if (jpeg_ret != JPEG_ERR_OK) {
        result = jpeg_error_to_esp(jpeg_ret);
        goto cleanup;
    }

    int buffer_len = 0;
    jpeg_ret = jpeg_dec_get_outbuf_len(decoder, &buffer_len);
    if (jpeg_ret != JPEG_ERR_OK || buffer_len <= 0) {
        result = jpeg_ret == JPEG_ERR_OK ? ESP_ERR_INVALID_SIZE : jpeg_error_to_esp(jpeg_ret);
        goto cleanup;
    }

    uint8_t* decoded = (uint8_t*)heap_caps_aligned_alloc(16, buffer_len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (decoded == NULL) {
        decoded = (uint8_t*)heap_caps_aligned_alloc(16, buffer_len, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (decoded == NULL) {
        result = ESP_ERR_NO_MEM;
        goto cleanup;
    }
    memset(decoded, 0, buffer_len);

    io.outbuf = decoded;
    jpeg_ret = jpeg_dec_process(decoder, &io);
    if (jpeg_ret != JPEG_ERR_OK) {
        ESP_LOGE(TAG, "JPEG decode failed: %d", jpeg_ret);
        heap_caps_free(decoded);
        result = jpeg_error_to_esp(jpeg_ret);
        goto cleanup;
    }

    *out = decoded;
    *out_len = (size_t)buffer_len;
    *width = info.width;
    *height = info.height;
    *stride = (size_t)info.width * sizeof(uint16_t);

cleanup:
    jpeg_dec_close(decoder);
    if (result != ESP_OK) {
        clear_outputs(out, out_len, width, height, stride);
    }
    return result;
}

#endif  // CONFIG_IDF_TARGET_ESP32
