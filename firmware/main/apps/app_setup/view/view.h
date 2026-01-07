/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <smooth_lvgl.hpp>
#include <uitk/short_namespace.hpp>
#include <string_view>
#include <memory>
#include <vector>

namespace view {

class SelectMenuPage {
public:
    struct MenuItem {
        std::string label;
        std::function<void()> onClick;
    };

    struct MenuSection {
        std::string title;
        std::vector<MenuItem> items;
    };

    SelectMenuPage(std::vector<MenuSection> sections);

    void update();

private:
    std::vector<MenuSection> _sections;
    std::unique_ptr<uitk::lvgl_cpp::Container> _pannel;
    std::vector<std::unique_ptr<uitk::lvgl_cpp::Label>> _labels;
    std::vector<std::unique_ptr<uitk::lvgl_cpp::Button>> _buttons;
    int _pending_section_index = -1;
    int _pending_item_index    = -1;

    void create_selection_label(int x, int y, std::string_view text);
    void create_item_button(int y, const MenuItem& item, int section_idx, int item_idx);
};

}  // namespace view
