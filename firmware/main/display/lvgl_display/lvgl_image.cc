#include "lvgl_image.h"

#include <cstring>
#include <stdexcept>

#include <cbin_font.h>
#include <esp_heap_caps.h>
#include <esp_log.h>

namespace {

constexpr char kTag[] = "LvglImage";
constexpr uint8_t kGifHeader[] = {'G', 'I', 'F'};

void InitDescriptor(lv_img_dsc_t& descriptor, const void* data, size_t size)
{
    std::memset(&descriptor, 0, sizeof(descriptor));
    descriptor.data_size = size;
    descriptor.data = static_cast<const uint8_t*>(data);
    descriptor.header.magic = LV_IMAGE_HEADER_MAGIC;
}

}  // namespace

LvglRawImage::LvglRawImage(void* data, size_t size)
{
    InitDescriptor(image_dsc_, data, size);
    image_dsc_.header.cf = LV_COLOR_FORMAT_RAW_ALPHA;
}

bool LvglRawImage::IsGif() const
{
    return image_dsc_.data_size >= sizeof(kGifHeader) &&
           std::memcmp(image_dsc_.data, kGifHeader, sizeof(kGifHeader)) == 0;
}

LvglCBinImage::LvglCBinImage(void* data)
{
    image_dsc_ = cbin_img_dsc_create(static_cast<uint8_t*>(data));
}

LvglCBinImage::~LvglCBinImage()
{
    if (image_dsc_ != nullptr) {
        cbin_img_dsc_delete(image_dsc_);
    }
}

LvglAllocatedImage::LvglAllocatedImage(void* data, size_t size)
{
    InitDescriptor(image_dsc_, data, size);
    if (lv_image_decoder_get_info(&image_dsc_, &image_dsc_.header) != LV_RESULT_OK) {
        ESP_LOGE(kTag, "Unable to read image metadata: data=%p size=%u", data, static_cast<unsigned>(size));
        throw std::runtime_error("Unable to read image metadata");
    }
}

LvglAllocatedImage::LvglAllocatedImage(void* data, size_t size, int width, int height, int stride, int color_format)
{
    InitDescriptor(image_dsc_, data, size);
    image_dsc_.header.cf = color_format;
    image_dsc_.header.w = width;
    image_dsc_.header.h = height;
    image_dsc_.header.stride = stride;
}

LvglAllocatedImage::~LvglAllocatedImage()
{
    if (image_dsc_.data != nullptr) {
        heap_caps_free(const_cast<uint8_t*>(image_dsc_.data));
        image_dsc_.data = nullptr;
    }
}
