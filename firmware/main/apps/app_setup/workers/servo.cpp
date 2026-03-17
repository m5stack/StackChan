/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "workers.h"
#include <stackchan/stackchan.h>
#include <apps/common/toast/toast.h>
#include <mooncake_log.h>
#include <assets/assets.h>
#include <hal/hal.h>

using namespace smooth_ui_toolkit::lvgl_cpp;
using namespace setup_workers;

static std::string _tag = "Setup-Servo";

/**
 * @brief
 *
 */
class PageTips : public WorkerBase {
public:
    PageTips()
    {
        _panel = std::make_unique<Container>(lv_screen_active());
        _panel->setBgColor(lv_color_hex(0xEDF4FF));
        _panel->align(LV_ALIGN_CENTER, 0, 0);
        _panel->setBorderWidth(0);
        _panel->setSize(320, 240);
        _panel->setRadius(0);

        _title = std::make_unique<Label>(lv_screen_active());
        _title->setTextFont(&lv_font_montserrat_20);
        _title->setTextColor(lv_color_hex(0x7E7B9C));
        _title->align(LV_ALIGN_TOP_MID, 0, 13);
        _title->setText("HOME POSITION:");

        _img = std::make_unique<Image>(lv_screen_active());
        _img->setSrc(&setup_stackchan_front_view);
        _img->align(LV_ALIGN_CENTER, -74, 15);

        _btn_next = std::make_unique<Button>(lv_screen_active());
        apply_button_common_style(*_btn_next);
        _btn_next->align(LV_ALIGN_CENTER, 79, 73);
        _btn_next->setSize(120, 48);
        _btn_next->label().setText("Continue");
        _btn_next->label().setTextFont(&lv_font_montserrat_20);
        _btn_next->onClick().connect([this]() { _is_done = true; });

        _info = std::make_unique<Label>(lv_screen_active());
        _info->setTextFont(&lv_font_montserrat_20);
        _info->setTextColor(lv_color_hex(0x26206A));
        _info->align(LV_ALIGN_TOP_LEFT, 185, 56);
        _info->setTextAlign(LV_TEXT_ALIGN_LEFT);
        _info->setText("StackChan\nlooking\nstraight\nforward.");
    }

private:
    std::unique_ptr<uitk::lvgl_cpp::Container> _panel;
    std::unique_ptr<uitk::lvgl_cpp::Label> _title;
    std::unique_ptr<uitk::lvgl_cpp::Image> _img;
    std::unique_ptr<uitk::lvgl_cpp::Button> _btn_next;
    std::unique_ptr<uitk::lvgl_cpp::Label> _info;
};

/**
 * @brief
 *
 */
class PageCalibration : public WorkerBase {
public:
    PageCalibration()
    {
        _panel = std::make_unique<Container>(lv_screen_active());
        _panel->setBgColor(lv_color_hex(0xFFFFFF));
        _panel->align(LV_ALIGN_CENTER, 0, 0);
        _panel->setBorderWidth(0);
        _panel->setSize(320, 240);
        _panel->setRadius(0);
        _panel->setFlexFlow(LV_FLEX_FLOW_COLUMN);
        _panel->setFlexAlign(LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        _panel->setPadding(30, 30, 0, 0);
        _panel->setPadRow(24);

        _btn_go_home = std::make_unique<Button>(*_panel);
        apply_button_common_style(*_btn_go_home);
        _btn_go_home->setSize(290, 70);
        _btn_go_home->label().setText("Move To Home");
        _btn_go_home->onClick().connect([this]() { _go_home_flag = true; });

        _btn_confirm = std::make_unique<Button>(*_panel);
        apply_button_common_style(*_btn_confirm);
        _btn_confirm->setSize(290, 80);
        _btn_confirm->setBgColor(lv_color_hex(0xFFDF9A));
        _btn_confirm->label().setText("Set Current Position\nAs Home");
        _btn_confirm->label().setTextAlign(LV_TEXT_ALIGN_CENTER);
        _btn_confirm->label().setTextColor(lv_color_hex(0x47330A));
        _btn_confirm->onClick().connect([this]() { _confirm_flag = true; });

        _btn_reset_default = std::make_unique<Button>(*_panel);
        apply_button_common_style(*_btn_reset_default);
        _btn_reset_default->setSize(290, 80);
        _btn_reset_default->setBgColor(lv_color_hex(0xBAE4BA));
        _btn_reset_default->label().setText("Reset To Default\nHome Position");
        _btn_reset_default->label().setTextAlign(LV_TEXT_ALIGN_CENTER);
        _btn_reset_default->label().setTextColor(lv_color_hex(0x233B23));
        _btn_reset_default->onClick().connect([this]() { _reset_default_flag = true; });

        _btn_quit = std::make_unique<Button>(*_panel);
        apply_button_common_style(*_btn_quit);
        _btn_quit->setSize(230, 55);
        _btn_quit->label().setText("Done");
        _btn_quit->onClick().connect([this]() { _is_done = true; });

        auto& motion = GetStackChan().motion();
        motion.setAutoAngleSyncEnabled(true);
    }

    void update() override
    {
        if (_confirm_flag) {
            _confirm_flag = false;

            mclog::tagInfo(_tag, "set current angle as zero");

            auto& motion = GetStackChan().motion();
            motion.yawServo().setCurrentAngleAsZero();
            motion.pitchServo().setCurrentAngleAsZero();

            view::pop_a_toast("Home position set", view::ToastType::Success);
        }

        if (_go_home_flag) {
            _go_home_flag = false;

            view::pop_a_toast("Moving to home", view::ToastType::Warning);
            mclog::tagInfo(_tag, "go home");

            auto& motion = GetStackChan().motion();
            motion.goHome(666);
        }

        if (_reset_default_flag) {
            _reset_default_flag = false;

            mclog::tagInfo(_tag, "home reset");

            auto& motion = GetStackChan().motion();
            motion.yawServo().resetZeroCalibration();
            motion.pitchServo().resetZeroCalibration();

            view::pop_a_toast("Home position reset", view::ToastType::Success);
        }
    }

private:
    std::unique_ptr<uitk::lvgl_cpp::Container> _panel;
    std::unique_ptr<uitk::lvgl_cpp::Button> _btn_quit;
    std::unique_ptr<uitk::lvgl_cpp::Button> _btn_confirm;
    std::unique_ptr<uitk::lvgl_cpp::Button> _btn_go_home;
    std::unique_ptr<uitk::lvgl_cpp::Button> _btn_reset_default;
    bool _confirm_flag       = false;
    bool _go_home_flag       = false;
    bool _reset_default_flag = false;
};

ZeroCalibrationWorker::ZeroCalibrationWorker()
{
    _page_tips = std::make_unique<PageTips>();
}

void ZeroCalibrationWorker::update()
{
    // Page tips
    if (_page_tips) {
        _page_tips->update();
        if (_page_tips->isDone()) {
            _page_tips.reset();
            _page_calibration = std::make_unique<PageCalibration>();
        }
    }
    // Page calibration
    else if (_page_calibration) {
        _page_calibration->update();
        if (_page_calibration->isDone()) {
            _page_calibration.reset();
            _is_done = true;
        }
    }
}

struct RgbColorEntry {
    std::string name;
    uint8_t r;
    uint8_t g;
    uint8_t b;
};

static const std::vector<RgbColorEntry> _rgb_colors = {
    {"Red", 255, 0, 0},    {"Green", 0, 255, 0},     {"Blue", 0, 0, 255},      {"Yellow", 255, 255, 0},
    {"Cyan", 0, 255, 255}, {"Magenta", 255, 0, 255}, {"White", 255, 255, 255}, {"Off", 0, 0, 0},
};

RgbTestWorker::RgbTestWorker()
{
    _panel = std::make_unique<Container>(lv_screen_active());
    _panel->setBgColor(lv_color_hex(0xFFFFFF));
    _panel->align(LV_ALIGN_CENTER, 0, 0);
    _panel->setBorderWidth(0);
    _panel->setSize(320, 240);
    _panel->setRadius(0);
    _panel->setFlexFlow(LV_FLEX_FLOW_COLUMN);
    _panel->setFlexAlign(LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    _panel->setPadding(20, 20, 20, 20);
    _panel->setPadRow(15);

    for (const auto& color : _rgb_colors) {
        auto btn = std::make_unique<Button>(*_panel);
        apply_button_common_style(*btn);
        btn->setSize(200, 50);
        btn->label().setText(color.name);

        uint8_t r = color.r;
        uint8_t g = color.g;
        uint8_t b = color.b;
        btn->onClick().connect([r, g, b]() {
            GetStackChan().leftNeonLight().setColor(r, g, b);
            GetStackChan().rightNeonLight().setColor(r, g, b);
        });

        _buttons.push_back(std::move(btn));
    }

    auto btn_quit = std::make_unique<Button>(*_panel);
    apply_button_common_style(*btn_quit);
    btn_quit->setSize(200, 50);
    btn_quit->label().setText("Quit");
    btn_quit->onClick().connect([this]() { _is_done = true; });
    _buttons.push_back(std::move(btn_quit));
}

RgbTestWorker::~RgbTestWorker()
{
    GetStackChan().leftNeonLight().setColor(0, 0, 0);
    GetStackChan().rightNeonLight().setColor(0, 0, 0);
}
