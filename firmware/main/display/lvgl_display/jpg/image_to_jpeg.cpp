#include "image_to_jpeg.h"

#ifndef CONFIG_IDF_TARGET_ESP32

#include <esp_heap_caps.h>
#include <esp_imgfx_color_convert.h>
#include <esp_jpeg_enc.h>
#include <esp_log.h>

#include <algorithm>
#include <cstring>
#include <memory>

namespace {

constexpr const char* kTag = "JpegEncode";
constexpr size_t kJpegOutputSlackBytes = 4096;

struct AlignedFree {
    void operator()(uint8_t* data) const
    {
        if (data != nullptr) {
            jpeg_free_align(data);
        }
    }
};

using AlignedBuffer = std::unique_ptr<uint8_t, AlignedFree>;

struct PreparedImage {
    AlignedBuffer data;
    int size = 0;
    jpeg_pixel_format_t pixel_format = JPEG_PIXEL_FORMAT_RGB888;
    jpeg_subsampling_t subsampling = JPEG_SUBSAMPLE_420;
};

size_t RawSize(uint16_t width, uint16_t height, v4l2_pix_fmt_t format)
{
    const size_t pixels = static_cast<size_t>(width) * height;
    switch (format) {
        case V4L2_PIX_FMT_GREY:
            return pixels;
        case V4L2_PIX_FMT_RGB565:
        case V4L2_PIX_FMT_RGB565X:
        case V4L2_PIX_FMT_YUYV:
            return pixels * 2;
        case V4L2_PIX_FMT_RGB24:
            return pixels * 3;
        case V4L2_PIX_FMT_YUV420:
            return pixels * 3 / 2;
        default:
            return 0;
    }
}

AlignedBuffer CopyAligned(const uint8_t* src, size_t len)
{
    auto* dst = static_cast<uint8_t*>(jpeg_calloc_align(len, 16));
    if (dst == nullptr) {
        return nullptr;
    }
    std::memcpy(dst, src, len);
    return AlignedBuffer(dst);
}

esp_imgfx_pixel_fmt_t SourceColorFormat(v4l2_pix_fmt_t format)
{
    switch (format) {
        case V4L2_PIX_FMT_RGB565:
            return ESP_IMGFX_PIXEL_FMT_RGB565_LE;
        case V4L2_PIX_FMT_RGB565X:
            return ESP_IMGFX_PIXEL_FMT_RGB565_BE;
        case V4L2_PIX_FMT_RGB24:
            return ESP_IMGFX_PIXEL_FMT_RGB888;
        case V4L2_PIX_FMT_YUYV:
            return ESP_IMGFX_PIXEL_FMT_YUYV;
        case V4L2_PIX_FMT_GREY:
            return ESP_IMGFX_PIXEL_FMT_Y;
        case V4L2_PIX_FMT_YUV420:
            return ESP_IMGFX_PIXEL_FMT_I420;
        default:
            return ESP_IMGFX_PIXEL_FMT_RGB888;
    }
}

bool ConvertToRgb888(const uint8_t* src, size_t src_len, uint16_t width, uint16_t height, v4l2_pix_fmt_t format,
                     PreparedImage& image)
{
    const size_t out_len = static_cast<size_t>(width) * height * 3;
    AlignedBuffer out(static_cast<uint8_t*>(jpeg_calloc_align(out_len, 16)));
    if (out == nullptr) {
        return false;
    }

    esp_imgfx_color_convert_cfg_t config = {
        .in_res = {.width = static_cast<int16_t>(width), .height = static_cast<int16_t>(height)},
        .in_pixel_fmt = SourceColorFormat(format),
        .out_pixel_fmt = ESP_IMGFX_PIXEL_FMT_RGB888,
        .color_space_std = ESP_IMGFX_COLOR_SPACE_STD_BT601,
    };

    esp_imgfx_color_convert_handle_t handle = nullptr;
    esp_imgfx_err_t err = esp_imgfx_color_convert_open(&config, &handle);
    if (err != ESP_IMGFX_ERR_OK || handle == nullptr) {
        ESP_LOGE(kTag, "Color converter open failed: %d", err);
        return false;
    }

    esp_imgfx_data_t input = {
        .data = const_cast<uint8_t*>(src),
        .data_len = static_cast<uint32_t>(src_len),
    };
    esp_imgfx_data_t output = {
        .data = out.get(),
        .data_len = static_cast<uint32_t>(out_len),
    };
    err = esp_imgfx_color_convert_process(handle, &input, &output);
    esp_imgfx_color_convert_close(handle);
    if (err != ESP_IMGFX_ERR_OK) {
        ESP_LOGE(kTag, "Color conversion failed: %d", err);
        return false;
    }

    image.data = std::move(out);
    image.size = static_cast<int>(out_len);
    image.pixel_format = JPEG_PIXEL_FORMAT_RGB888;
    image.subsampling = JPEG_SUBSAMPLE_420;
    return true;
}

bool PrepareInput(uint8_t* src, size_t src_len, uint16_t width, uint16_t height, v4l2_pix_fmt_t format,
                  PreparedImage& image)
{
    if (src == nullptr || width == 0 || height == 0) {
        return false;
    }

    const size_t expected_len = RawSize(width, height, format);
    if (expected_len == 0 || src_len < expected_len) {
        ESP_LOGE(kTag, "Invalid raw image: format=0x%08lx len=%u expected=%u",
                 static_cast<unsigned long>(format),
                 static_cast<unsigned>(src_len),
                 static_cast<unsigned>(expected_len));
        return false;
    }

    switch (format) {
        case V4L2_PIX_FMT_GREY:
            image.data = CopyAligned(src, expected_len);
            image.size = static_cast<int>(expected_len);
            image.pixel_format = JPEG_PIXEL_FORMAT_GRAY;
            image.subsampling = JPEG_SUBSAMPLE_GRAY;
            return image.data != nullptr;
        case V4L2_PIX_FMT_YUYV:
            image.data = CopyAligned(src, expected_len);
            image.size = static_cast<int>(expected_len);
            image.pixel_format = JPEG_PIXEL_FORMAT_YCbYCr;
            image.subsampling = JPEG_SUBSAMPLE_422;
            return image.data != nullptr;
        case V4L2_PIX_FMT_RGB24:
            image.data = CopyAligned(src, expected_len);
            image.size = static_cast<int>(expected_len);
            image.pixel_format = JPEG_PIXEL_FORMAT_RGB888;
            image.subsampling = JPEG_SUBSAMPLE_420;
            return image.data != nullptr;
        case V4L2_PIX_FMT_RGB565:
        case V4L2_PIX_FMT_RGB565X:
        case V4L2_PIX_FMT_YUV420:
            return ConvertToRgb888(src, expected_len, width, height, format, image);
        default:
            ESP_LOGE(kTag, "Unsupported image format: 0x%08lx", static_cast<unsigned long>(format));
            return false;
    }
}

bool CopyJpeg(uint8_t* src, size_t src_len, uint8_t** out, size_t* out_len)
{
    if (src == nullptr || src_len == 0 || out == nullptr || out_len == nullptr) {
        return false;
    }
    auto* copy = static_cast<uint8_t*>(heap_caps_malloc(src_len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (copy == nullptr) {
        copy = static_cast<uint8_t*>(heap_caps_malloc(src_len, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    }
    if (copy == nullptr) {
        return false;
    }
    std::memcpy(copy, src, src_len);
    *out = copy;
    *out_len = src_len;
    return true;
}

}  // namespace

bool image_to_jpeg(uint8_t* src, size_t src_len, uint16_t width, uint16_t height, v4l2_pix_fmt_t format,
                   uint8_t quality, uint8_t** out, size_t* out_len)
{
    if (out == nullptr || out_len == nullptr) {
        return false;
    }
    *out = nullptr;
    *out_len = 0;

    if (format == V4L2_PIX_FMT_JPEG) {
        return CopyJpeg(src, src_len, out, out_len);
    }

    PreparedImage input;
    if (!PrepareInput(src, src_len, width, height, format, input)) {
        return false;
    }

    jpeg_enc_config_t config = DEFAULT_JPEG_ENC_CONFIG();
    config.width = width;
    config.height = height;
    config.src_type = input.pixel_format;
    config.subsampling = input.subsampling;
    config.quality = std::clamp<uint8_t>(quality, 1, 100);
    config.rotate = JPEG_ROTATE_0D;
    config.task_enable = false;

    jpeg_enc_handle_t encoder = nullptr;
    jpeg_error_t jpeg_ret = jpeg_enc_open(&config, &encoder);
    if (jpeg_ret != JPEG_ERR_OK || encoder == nullptr) {
        ESP_LOGE(kTag, "JPEG encoder open failed: %d", jpeg_ret);
        return false;
    }

    const size_t cap = std::max<size_t>(static_cast<size_t>(width) * height * 3 + kJpegOutputSlackBytes, 16 * 1024);
    auto* encoded = static_cast<uint8_t*>(heap_caps_malloc(cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (encoded == nullptr) {
        encoded = static_cast<uint8_t*>(heap_caps_malloc(cap, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    }
    if (encoded == nullptr) {
        jpeg_enc_close(encoder);
        return false;
    }

    int encoded_len = 0;
    jpeg_ret = jpeg_enc_process(encoder, input.data.get(), input.size, encoded, static_cast<int>(cap), &encoded_len);
    jpeg_enc_close(encoder);
    if (jpeg_ret != JPEG_ERR_OK || encoded_len <= 0) {
        ESP_LOGE(kTag, "JPEG encode failed: %d", jpeg_ret);
        heap_caps_free(encoded);
        return false;
    }

    *out = encoded;
    *out_len = static_cast<size_t>(encoded_len);
    return true;
}

bool image_to_jpeg_cb(uint8_t* src, size_t src_len, uint16_t width, uint16_t height, v4l2_pix_fmt_t format,
                      uint8_t quality, jpg_out_cb cb, void* arg)
{
    if (cb == nullptr) {
        return false;
    }

    uint8_t* encoded = nullptr;
    size_t encoded_len = 0;
    if (!image_to_jpeg(src, src_len, width, height, format, quality, &encoded, &encoded_len)) {
        return false;
    }

    const size_t consumed = cb(arg, 0, encoded, encoded_len);
    heap_caps_free(encoded);
    return consumed == encoded_len;
}

#endif  // CONFIG_IDF_TARGET_ESP32
