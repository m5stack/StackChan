#include "lvgl_theme.h"

#include <cstdlib>
#include <utility>

LvglTheme::LvglTheme(const std::string& name)
    : Theme(name)
{
}

lv_color_t LvglTheme::ParseColor(const std::string& color)
{
    if (color.size() != 7 || color[0] != '#') {
        return lv_color_black();
    }

    const uint8_t r = static_cast<uint8_t>(std::strtoul(color.substr(1, 2).c_str(), nullptr, 16));
    const uint8_t g = static_cast<uint8_t>(std::strtoul(color.substr(3, 2).c_str(), nullptr, 16));
    const uint8_t b = static_cast<uint8_t>(std::strtoul(color.substr(5, 2).c_str(), nullptr, 16));
    return lv_color_make(r, g, b);
}

LvglThemeManager& LvglThemeManager::GetInstance()
{
    static LvglThemeManager instance;
    return instance;
}

void LvglThemeManager::RegisterTheme(const std::string& theme_name, LvglTheme* theme)
{
    themes_[theme_name] = theme;
}

LvglTheme* LvglThemeManager::GetTheme(const std::string& theme_name)
{
    const auto it = themes_.find(theme_name);
    return it != themes_.end() ? it->second : nullptr;
}
