/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "workers.h"
#include <stackchan/stackchan.h>
#include <mooncake_log.h>
#include <hal/hal.h>

using namespace smooth_ui_toolkit::lvgl_cpp;
using namespace setup_workers;

static std::string _tag = "Setup-Servo";

ZeroCalibrationWorker::ZeroCalibrationWorker()
{
    _pannel = std::make_unique<Container>(lv_screen_active());
    _pannel->setBgColor(lv_color_hex(0xFFFFFF));
    _pannel->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
    _pannel->align(LV_ALIGN_CENTER, 0, 0);
    _pannel->setBorderWidth(0);
    _pannel->setSize(320, 240);
    _pannel->setRadius(0);

    _btn_go_home = std::make_unique<Button>(lv_screen_active());
    apply_button_common_style(*_btn_go_home);
    _btn_go_home->setAlign(LV_ALIGN_CENTER);
    _btn_go_home->setPos(0, -85);
    _btn_go_home->setSize(230, 55);
    _btn_go_home->label().setText("Move To Home");
    _btn_go_home->onClick().connect([this]() { _go_home_flag = true; });

    _btn_confirm = std::make_unique<Button>(lv_screen_active());
    apply_button_common_style(*_btn_confirm);
    _btn_confirm->setAlign(LV_ALIGN_CENTER);
    _btn_confirm->setPos(0, 0);
    _btn_confirm->setSize(290, 70);
    _btn_confirm->label().setText("Set Current Position\nAs Home");
    _btn_confirm->label().setTextAlign(LV_TEXT_ALIGN_CENTER);
    _btn_confirm->onClick().connect([this]() { _confirm_flag = true; });

    _btn_quit = std::make_unique<Button>(lv_screen_active());
    apply_button_common_style(*_btn_quit);
    _btn_quit->setAlign(LV_ALIGN_CENTER);
    _btn_quit->setPos(0, 85);
    _btn_quit->setSize(230, 55);
    _btn_quit->label().setText("Done");
    _btn_quit->onClick().connect([this]() { _is_done = true; });

    auto& motion = GetStackChan().motion();
    motion.setAutoAngleSyncEnabled(true);
}

void ZeroCalibrationWorker::update()
{
    if (_confirm_flag) {
        _confirm_flag = false;

        mclog::tagInfo(_tag, "set current angle as zero");

        auto& motion = GetStackChan().motion();
        motion.yawServo().setCurrentAngleAsZero();
        motion.pitchServo().setCurrentAngleAsZero();
    }

    if (_go_home_flag) {
        _go_home_flag = false;

        mclog::tagInfo(_tag, "go home");

        auto& motion = GetStackChan().motion();
        motion.goHome(300);
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
    _pannel = std::make_unique<Container>(lv_screen_active());
    _pannel->setBgColor(lv_color_hex(0xFFFFFF));
    _pannel->align(LV_ALIGN_CENTER, 0, 0);
    _pannel->setBorderWidth(0);
    _pannel->setSize(320, 240);
    _pannel->setRadius(0);
    _pannel->setFlexFlow(LV_FLEX_FLOW_COLUMN);
    _pannel->setFlexAlign(LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    _pannel->setPadding(20, 20, 20, 20);
    _pannel->setPadRow(15);

    for (const auto& color : _rgb_colors) {
        auto btn = std::make_unique<Button>(*_pannel);
        apply_button_common_style(*btn);
        btn->setSize(200, 50);
        btn->label().setText(color.name);

        uint8_t r = color.r;
        uint8_t g = color.g;
        uint8_t b = color.b;
        btn->onClick().connect([r, g, b]() { GetHAL().showRgbColor(r, g, b); });

        _buttons.push_back(std::move(btn));
    }

    auto btn_quit = std::make_unique<Button>(*_pannel);
    apply_button_common_style(*btn_quit);
    btn_quit->setSize(200, 50);
    btn_quit->label().setText("Quit");
    btn_quit->onClick().connect([this]() { _is_done = true; });
    _buttons.push_back(std::move(btn_quit));
}

RgbTestWorker::~RgbTestWorker()
{
    GetHAL().showRgbColor(0, 0, 0);
}
