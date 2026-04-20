/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <hal/hal.h>
#include <smooth_lvgl.hpp>
#include <uitk/short_namespace.hpp>
#include <assets/assets.h>
#include <mooncake_log.h>
#include <functional>
#include <vector>
#include <string>
#include <memory>

namespace view {

/**
 * @brief
 *
 */
class AppListPage {
public:
    AppListPage(const app_center::AppInfoList_t& app_list)

    {
        _panel = std::make_unique<uitk::lvgl_cpp::Container>(lv_screen_active());
        _panel->setSize(320, 240);
        _panel->setAlign(LV_ALIGN_CENTER);
        _panel->setBgColor(lv_color_hex(0xFFFAD6));
        _panel->setFlexFlow(LV_FLEX_FLOW_COLUMN);
        _panel->setFlexAlign(LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        _panel->setPadding(52, 72, 0, 0);
        _panel->setPadRow(18);
        _panel->setRadius(0);
        _panel->setBorderWidth(0);
        _panel->setScrollbarMode(LV_SCROLLBAR_MODE_ACTIVE);

        _bg_image = assets::get_image("app_center_bg.png");
        if (_bg_image.data_size != 0) {
            lv_obj_set_style_bg_image_src(_panel->get(), &_bg_image, LV_PART_MAIN);
        }

        _title_panel = std::make_unique<uitk::lvgl_cpp::Container>(lv_screen_active());
        _title_panel->setSize(320, 28);
        _title_panel->align(LV_ALIGN_TOP_MID, 0, 0);
        _title_panel->setBgColor(lv_color_hex(0xF4A354));
        _title_panel->setPadding(0, 0, 0, 0);
        _title_panel->setRadius(0);
        _title_panel->setBorderWidth(0);
        _title_panel->setScrollbarMode(LV_SCROLLBAR_MODE_ACTIVE);

        _title = std::make_unique<uitk::lvgl_cpp::Label>(*_title_panel);
        _title->setText("App Center");
        _title->setTextFont(&lv_font_montserrat_16);
        _title->setTextColor(lv_color_hex(0xFFF8C7));
        _title->align(LV_ALIGN_CENTER, 0, 0);

        int index = 0;
        for (const auto& app : app_list) {
            auto btn = std::make_unique<uitk::lvgl_cpp::Button>(*_panel);
            btn->setSize(282, 52);
            btn->setRadius(18);
            btn->setBorderWidth(0);
            btn->setShadowWidth(0);
            btn->setBgColor(lv_color_hex(0xFFDF9A));

            btn->label().setText(app.name);
            btn->label().setTextFont(&lv_font_montserrat_20);
            btn->label().setTextColor(lv_color_hex(0x47330A));
            btn->label().setLongMode(LV_LABEL_LONG_SCROLL_CIRCULAR);
            btn->label().setWidth(220);
            btn->label().setTextAlign(LV_TEXT_ALIGN_CENTER);
            btn->label().setAlign(LV_ALIGN_CENTER);

            btn->onClick().connect([this, index]() { _clicked_index = index; });

            _buttons.push_back(std::move(btn));
            index++;
        }
    }

    int isSelected()
    {
        int temp       = _clicked_index;
        _clicked_index = -1;
        return temp;
    }

private:
    std::unique_ptr<uitk::lvgl_cpp::Container> _panel;
    std::unique_ptr<uitk::lvgl_cpp::Container> _title_panel;
    std::unique_ptr<uitk::lvgl_cpp::Label> _title;
    std::vector<std::unique_ptr<uitk::lvgl_cpp::Button>> _buttons;
    lv_image_dsc_t _bg_image;

    int _clicked_index = -1;
};

/**
 * @brief
 *
 */
class AppDetailPage {
public:
    AppDetailPage(const app_center::AppInfo_t& app_info)
    {
        _panel = std::make_unique<uitk::lvgl_cpp::Container>(lv_screen_active());
        _panel->setBgColor(lv_color_hex(0xFFFAD6));
        _panel->align(LV_ALIGN_CENTER, 0, 0);
        _panel->setBorderWidth(0);
        _panel->setSize(320, 240);
        _panel->setRadius(0);
        _panel->setFlexFlow(LV_FLEX_FLOW_COLUMN);
        _panel->setFlexAlign(LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        _panel->setPadding(48, 72, 0, 0);
        _panel->setPadRow(18);
        _panel->setScrollbarMode(LV_SCROLLBAR_MODE_ACTIVE);

        _bg_image = assets::get_image("app_center_bg.png");
        if (_bg_image.data_size != 0) {
            lv_obj_set_style_bg_image_src(_panel->get(), &_bg_image, LV_PART_MAIN);
        }

        _title = std::make_unique<uitk::lvgl_cpp::Label>(*_panel);
        _title->setText(app_info.name);
        _title->setTextFont(&lv_font_montserrat_20);
        _title->setTextColor(lv_color_hex(0x47330A));
        _title->setTextAlign(LV_TEXT_ALIGN_CENTER);
        _title->setLongMode(LV_LABEL_LONG_SCROLL_CIRCULAR);
        _title->setWidth(220);
        _title->setHeight(28);

        _desc_label = std::make_unique<uitk::lvgl_cpp::Label>(*_panel);
        _desc_label->setText(app_info.description);
        _desc_label->setTextFont(&lv_font_montserrat_20);
        _desc_label->setTextColor(lv_color_hex(0x47330A));
        _desc_label->setTextAlign(LV_TEXT_ALIGN_LEFT);
        _desc_label->setWidth(260);
        _desc_label->setLongMode(LV_LABEL_LONG_WRAP);

        _btn_container = std::make_unique<uitk::lvgl_cpp::Container>(*_panel);
        _btn_container->setSize(LV_PCT(100), 80);
        _btn_container->setBgOpa(LV_OPA_TRANSP);
        _btn_container->setBorderWidth(0);
        _btn_container->setPadding(0, 0, 0, 0);
        _btn_container->setPadColumn(28);
        _btn_container->setFlexFlow(LV_FLEX_FLOW_ROW);
        _btn_container->setFlexAlign(LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        _btn_back = std::make_unique<uitk::lvgl_cpp::Button>(*_btn_container);
        _btn_back->setSize(112, 48);
        _btn_back->setRadius(18);
        _btn_back->setBgColor(lv_color_hex(0xFFDF9A));
        _btn_back->setBorderWidth(0);
        _btn_back->setShadowWidth(0);
        _btn_back->label().setText("Back");
        _btn_back->label().setTextFont(&lv_font_montserrat_20);
        _btn_back->label().setTextColor(lv_color_hex(0x47330A));
        _btn_back->onClick().connect([this]() { _is_back = true; });

        _btn_launch = std::make_unique<uitk::lvgl_cpp::Button>(*_btn_container);
        _btn_launch->setSize(112, 48);
        _btn_launch->setRadius(18);
        _btn_launch->setBgColor(lv_color_hex(0xFFAC6D));
        _btn_launch->setBorderWidth(0);
        _btn_launch->setShadowWidth(0);
        _btn_launch->label().setText("Launch");
        _btn_launch->label().setTextFont(&lv_font_montserrat_20);
        _btn_launch->label().setTextColor(lv_color_hex(0x47330A));
        _btn_launch->onClick().connect([this]() { _is_launch = true; });
    }

    bool checkBack()
    {
        if (_is_back) {
            _is_back = false;
            return true;
        }
        return false;
    }

    bool checkLaunch()
    {
        if (_is_launch) {
            _is_launch = false;
            return true;
        }
        return false;
    }

private:
    std::unique_ptr<uitk::lvgl_cpp::Container> _panel;
    std::unique_ptr<uitk::lvgl_cpp::Label> _title;
    std::unique_ptr<uitk::lvgl_cpp::Label> _desc_label;
    std::unique_ptr<uitk::lvgl_cpp::Container> _btn_container;
    std::unique_ptr<uitk::lvgl_cpp::Button> _btn_back;
    std::unique_ptr<uitk::lvgl_cpp::Button> _btn_launch;
    lv_image_dsc_t _bg_image;

    bool _is_back   = false;
    bool _is_launch = false;
};

/**
 * @brief
 *
 */
class AppInstallPage {
public:
    AppInstallPage(const app_center::AppInfo_t& app_info)
    {
        _panel = std::make_unique<uitk::lvgl_cpp::Container>(lv_screen_active());
        _panel->setBgColor(lv_color_hex(0xFFFAD6));
        _panel->align(LV_ALIGN_CENTER, 0, 0);
        _panel->setBorderWidth(0);
        _panel->setSize(320, 240);
        _panel->setRadius(0);
        _panel->setPadding(0, 0, 0, 0);

        _bg_image = assets::get_image("app_center_bg.png");
        if (_bg_image.data_size != 0) {
            lv_obj_set_style_bg_image_src(_panel->get(), &_bg_image, LV_PART_MAIN);
        }

        _title = std::make_unique<uitk::lvgl_cpp::Label>(*_panel);
        _title->setText(app_info.name);
        _title->setTextFont(&lv_font_montserrat_20);
        _title->setTextColor(lv_color_hex(0x47330A));
        _title->setTextAlign(LV_TEXT_ALIGN_CENTER);
        _title->align(LV_ALIGN_TOP_MID, 0, 15);
        _title->setLongMode(LV_LABEL_LONG_SCROLL_CIRCULAR);
        _title->setWidth(220);
        _title->setHeight(28);

        _tips = std::make_unique<uitk::lvgl_cpp::Label>(*_panel);
        _tips->setText("Downloading...");
        _tips->setTextFont(&lv_font_montserrat_16);
        _tips->setTextColor(lv_color_hex(0xA36135));
        _tips->setTextAlign(LV_TEXT_ALIGN_CENTER);
        _tips->setLongMode(LV_LABEL_LONG_SCROLL_CIRCULAR);
        _tips->align(LV_ALIGN_CENTER, 0, -40);

        _progress_bar = std::make_unique<uitk::lvgl_cpp::Bar>(*_panel);
        _progress_bar->setSize(260, 92);
        _progress_bar->setRadius(18);
        _progress_bar->setRadius(0, LV_PART_INDICATOR);
        _progress_bar->align(LV_ALIGN_CENTER, 0, 23);
        _progress_bar->setBgColor(lv_color_hex(0xFFDF9A));
        _progress_bar->setBgColor(lv_color_hex(0xFF9E5D), LV_PART_INDICATOR);
        _progress_bar->setBgOpa(LV_OPA_COVER);
        _progress_bar->setRange(0, 100);
        _progress_bar->setValue(0);

        _progress = std::make_unique<uitk::lvgl_cpp::Label>(*_panel);
        _progress->setTextFont(&lv_font_montserrat_24);
        _progress->setTextColor(lv_color_hex(0x47330A));
        _progress->align(LV_ALIGN_CENTER, 0, 23);
        _progress->setText("");
    }

    void setProgress(int percent)
    {
        _progress->setText(fmt::format("{}%", percent));
        _progress_bar->setValue(percent);
    }

private:
    std::unique_ptr<uitk::lvgl_cpp::Container> _panel;
    std::unique_ptr<uitk::lvgl_cpp::Label> _title;
    std::unique_ptr<uitk::lvgl_cpp::Label> _tips;
    std::unique_ptr<uitk::lvgl_cpp::Label> _progress;
    std::unique_ptr<uitk::lvgl_cpp::Bar> _progress_bar;
    lv_image_dsc_t _bg_image;
};

}  // namespace view
