#pragma once

#include <lvgl.h>

class LvglFont {
public:
    virtual ~LvglFont() = default;
    virtual const lv_font_t* font() const = 0;
};

class LvglBuiltInFont final : public LvglFont {
public:
    explicit LvglBuiltInFont(const lv_font_t* font) : font_(font) {}

    const lv_font_t* font() const override { return font_; }

private:
    const lv_font_t* font_ = nullptr;
};

class LvglCBinFont final : public LvglFont {
public:
    explicit LvglCBinFont(void* data);
    ~LvglCBinFont() override;

    const lv_font_t* font() const override { return font_; }

private:
    lv_font_t* font_ = nullptr;
};
