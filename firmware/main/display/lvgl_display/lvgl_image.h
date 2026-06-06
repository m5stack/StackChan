#pragma once

#include <cstddef>

#include <lvgl.h>

class LvglImage {
public:
    virtual ~LvglImage() = default;
    virtual const lv_img_dsc_t* image_dsc() const = 0;
    virtual bool IsGif() const { return false; }
};

class LvglRawImage final : public LvglImage {
public:
    LvglRawImage(void* data, size_t size);

    const lv_img_dsc_t* image_dsc() const override { return &image_dsc_; }
    bool IsGif() const override;

private:
    lv_img_dsc_t image_dsc_{};
};

class LvglCBinImage final : public LvglImage {
public:
    explicit LvglCBinImage(void* data);
    ~LvglCBinImage() override;

    const lv_img_dsc_t* image_dsc() const override { return image_dsc_; }

private:
    lv_img_dsc_t* image_dsc_ = nullptr;
};

class LvglSourceImage final : public LvglImage {
public:
    explicit LvglSourceImage(const lv_img_dsc_t* image_dsc) : image_dsc_(image_dsc) {}

    const lv_img_dsc_t* image_dsc() const override { return image_dsc_; }

private:
    const lv_img_dsc_t* image_dsc_ = nullptr;
};

class LvglAllocatedImage final : public LvglImage {
public:
    LvglAllocatedImage(void* data, size_t size);
    LvglAllocatedImage(void* data, size_t size, int width, int height, int stride, int color_format);
    ~LvglAllocatedImage() override;

    const lv_img_dsc_t* image_dsc() const override { return &image_dsc_; }

private:
    lv_img_dsc_t image_dsc_{};
};
