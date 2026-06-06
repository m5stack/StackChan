#pragma once

#include <map>
#include <memory>
#include <string>
#include <utility>

#include <lvgl.h>

#include "display.h"
#include "emoji_collection.h"
#include "lvgl_font.h"
#include "lvgl_image.h"

class LvglTheme final : public Theme {
public:
    explicit LvglTheme(const std::string& name);

    static lv_color_t ParseColor(const std::string& color);

    lv_color_t background_color() const { return background_color_; }
    lv_color_t text_color() const { return text_color_; }
    lv_color_t chat_background_color() const { return chat_background_color_; }
    lv_color_t user_bubble_color() const { return user_bubble_color_; }
    lv_color_t assistant_bubble_color() const { return assistant_bubble_color_; }
    lv_color_t system_bubble_color() const { return system_bubble_color_; }
    lv_color_t system_text_color() const { return system_text_color_; }
    lv_color_t border_color() const { return border_color_; }
    lv_color_t low_battery_color() const { return low_battery_color_; }
    std::shared_ptr<LvglImage> background_image() const { return background_image_; }
    std::shared_ptr<EmojiCollection> emoji_collection() const { return emoji_collection_; }
    std::shared_ptr<LvglFont> text_font() const { return text_font_; }
    std::shared_ptr<LvglFont> icon_font() const { return icon_font_; }
    int spacing(int scale) const { return spacing_ * scale; }

    void set_background_color(lv_color_t value) { background_color_ = value; }
    void set_text_color(lv_color_t value) { text_color_ = value; }
    void set_chat_background_color(lv_color_t value) { chat_background_color_ = value; }
    void set_user_bubble_color(lv_color_t value) { user_bubble_color_ = value; }
    void set_assistant_bubble_color(lv_color_t value) { assistant_bubble_color_ = value; }
    void set_system_bubble_color(lv_color_t value) { system_bubble_color_ = value; }
    void set_system_text_color(lv_color_t value) { system_text_color_ = value; }
    void set_border_color(lv_color_t value) { border_color_ = value; }
    void set_low_battery_color(lv_color_t value) { low_battery_color_ = value; }
    void set_background_image(std::shared_ptr<LvglImage> value) { background_image_ = std::move(value); }
    void set_emoji_collection(std::shared_ptr<EmojiCollection> value) { emoji_collection_ = std::move(value); }
    void set_text_font(std::shared_ptr<LvglFont> value) { text_font_ = std::move(value); }
    void set_icon_font(std::shared_ptr<LvglFont> value) { icon_font_ = std::move(value); }

private:
    int spacing_ = 2;
    lv_color_t background_color_{};
    lv_color_t text_color_{};
    lv_color_t chat_background_color_{};
    lv_color_t user_bubble_color_{};
    lv_color_t assistant_bubble_color_{};
    lv_color_t system_bubble_color_{};
    lv_color_t system_text_color_{};
    lv_color_t border_color_{};
    lv_color_t low_battery_color_{};
    std::shared_ptr<LvglImage> background_image_;
    std::shared_ptr<EmojiCollection> emoji_collection_;
    std::shared_ptr<LvglFont> text_font_;
    std::shared_ptr<LvglFont> icon_font_;
};

class LvglThemeManager {
public:
    static LvglThemeManager& GetInstance();

    void RegisterTheme(const std::string& theme_name, LvglTheme* theme);
    LvglTheme* GetTheme(const std::string& theme_name);

private:
    std::map<std::string, LvglTheme*> themes_;
};
