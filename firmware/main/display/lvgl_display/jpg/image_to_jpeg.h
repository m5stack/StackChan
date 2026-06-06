#pragma once

#include "sdkconfig.h"

#ifndef CONFIG_IDF_TARGET_ESP32

#include <stddef.h>
#include <stdint.h>

#if defined(CONFIG_IDF_TARGET_ESP32P4) || defined(CONFIG_IDF_TARGET_ESP32S3)
#include <linux/videodev2.h>
#else
#define V4L2_PIX_FMT_RGB565 0x50424752
#define V4L2_PIX_FMT_RGB565X 0x52474250
#define V4L2_PIX_FMT_RGB24 0x33424752
#define V4L2_PIX_FMT_YUYV 0x56595559
#define V4L2_PIX_FMT_YUV420 0x32315559
#define V4L2_PIX_FMT_GREY 0x59455247
#define V4L2_PIX_FMT_JPEG 0x4745504A
#endif

typedef uint32_t v4l2_pix_fmt_t;
typedef size_t (*jpg_out_cb)(void* arg, size_t index, const void* data, size_t len);

bool image_to_jpeg(uint8_t* src, size_t src_len, uint16_t width, uint16_t height, v4l2_pix_fmt_t format,
                   uint8_t quality, uint8_t** out, size_t* out_len);
bool image_to_jpeg_cb(uint8_t* src, size_t src_len, uint16_t width, uint16_t height, v4l2_pix_fmt_t format,
                      uint8_t quality, jpg_out_cb cb, void* arg);

#endif  // CONFIG_IDF_TARGET_ESP32
