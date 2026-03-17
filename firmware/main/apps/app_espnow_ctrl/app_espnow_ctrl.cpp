/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_espnow_ctrl.h"
#include "view/page_selector.h"
#include "view/view.h"
#include <hal/hal.h>
#include <mooncake.h>
#include <mooncake_log.h>
#include <assets/assets.h>
#include <smooth_lvgl.hpp>
#include <stackchan/stackchan.h>
#include <apps/common/common.h>
#include <cstdint>
#include <mutex>
#include <string>

using namespace mooncake;
using namespace smooth_ui_toolkit::lvgl_cpp;
using namespace stackchan;

AppEspnowControl::AppEspnowControl()
{
    // 配置 App 名
    setAppInfo().name = "ESPNOW.REMOTE";
    // 配置 App 图标
    setAppInfo().icon = (void*)&icon_controller;
    // 配置 App 主题颜色
    static uint32_t theme_color = 0x7ACE74;
    setAppInfo().userData       = (void*)&theme_color;
}

void AppEspnowControl::onCreate()
{
    mclog::tagInfo(getAppInfo().name, "on create");
}

static std::mutex _mutex;
static std::vector<uint8_t> _received_data;
static int _receiver_id  = 0;
static int _wifi_channel = 0;
static bool _is_receiver = false;

void AppEspnowControl::onOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");

    // Default setup
    _receiver_id  = 1;
    _wifi_channel = 1;

    // Start setup page
    bool is_advanced = start_startup_page();
    if (is_advanced) {
        start_advanced_page();
    }
    mclog::tagInfo(getAppInfo().name, "get setup: role {}, id {}, channel {}", _is_receiver ? "Receiver" : "Sender",
                   _receiver_id, _wifi_channel);

    // Start espnow
    GetHAL().startEspNow(_wifi_channel);
    GetHAL().onEspNowData.connect([](const std::vector<uint8_t>& data) {
        std::lock_guard<std::mutex> lock(_mutex);
        _received_data = data;
    });

    // Normal avatar
    LvglLockGuard lock;

    auto& stackchan = GetStackChan();

    auto avatar = std::make_unique<avatar::DefaultAvatar>();
    avatar->init(lv_screen_active());
    stackchan.attachAvatar(std::move(avatar));

    stackchan.clearModifiers();
    stackchan.addModifier(std::make_unique<BreathModifier>());
    stackchan.addModifier(std::make_unique<BlinkModifier>());

    // Diable auto angle sync to prevent jitter
    stackchan.motion().setAutoAngleSyncEnabled(false);

    GetHAL().setLaserEnabled(false);

    view::create_home_indicator([&]() { close(); }, 0xA0D99C, 0x154311);
    view::create_status_bar(0xA0D99C, 0x154311);
}

bool AppEspnowControl::start_startup_page()
{
    GetHAL().lvglLock();
    auto page = std::make_unique<view::EspnowRoleSelectorPage>();
    GetHAL().lvglUnlock();
    while (1) {
        GetHAL().delay(50);
        LvglLockGuard lock;
        if (page->isSelected()) {
            break;
        }
    }

    bool is_advanced = false;
    GetHAL().lvglLock();
    if (page->selectedIndex() == 0) {
        _is_receiver = true;
    } else if (page->selectedIndex() == 1) {
        _is_receiver = false;
    } else {
        is_advanced = true;
    }
    page.reset();
    GetHAL().lvglUnlock();

    return is_advanced;
}

void AppEspnowControl::start_advanced_page()
{
    // Get role
    std::vector<std::string> role_options = {"Receiver", "Sender"};
    int role_selection                    = view::create_page_selector_and_wait("Select Role", role_options);
    _is_receiver                          = (role_selection == 0);
    mclog::tagInfo(getAppInfo().name, "selected role: {}", _is_receiver ? "Receiver" : "Sender");

    // Get wifi channel
    std::vector<std::string> channel_options;
    for (int i = 0; i < 13; i++) {
        channel_options.push_back(std::to_string(i + 1));
    }
    _wifi_channel = view::create_page_selector_and_wait("Select WiFi Channel", channel_options) + 1;
    mclog::tagInfo(getAppInfo().name, "selected wifi channel: {}", _wifi_channel);

    // Get id
    if (_is_receiver) {
        std::vector<std::string> id_options;
        for (int i = 1; i < 255; i++) {
            id_options.push_back(std::to_string(i));
        }
        _receiver_id = view::create_page_selector_and_wait("Select Receiver ID", id_options) + 1;
        mclog::tagInfo(getAppInfo().name, "selected receiver id: {}", _receiver_id);
    } else {
        std::vector<std::string> id_options;
        for (int i = 0; i < 255; i++) {
            if (i == 0) {
                id_options.push_back("0 (Broadcast)");
                continue;
            }
            id_options.push_back(std::to_string(i));
        }
        _receiver_id = view::create_page_selector_and_wait("Select Receiver ID", id_options);
        mclog::tagInfo(getAppInfo().name, "selected target id: {}", _receiver_id);
    }
}

void handle_received_data()
{
    std::lock_guard<std::mutex> lock(_mutex);

    // [target-id (uint8)] [yaw-angle (int16)] [pitch-angle (int16)] [speed (int16)] [laser-enabled (uint8)]
    // id: 0 for broadcast
    // yaw: -1280 ~ 1280
    // pitch: 0 ~ 900
    // speed: 0 ~ 1000, suggest 600
    // laser-enabled: 0 = off, 1 = on
    if (_received_data.size() >= 8) {
        uint8_t target_id = _received_data[0];
        if (target_id != 0 && target_id != _receiver_id) {
            mclog::info("not me, target id: {}", target_id);
            _received_data.clear();
            return;
        }

        int16_t yaw_angle   = static_cast<int16_t>(_received_data[1] | (_received_data[2] << 8));
        int16_t pitch_angle = static_cast<int16_t>(_received_data[3] | (_received_data[4] << 8));
        int16_t speed       = static_cast<int16_t>(_received_data[5] | (_received_data[6] << 8));
        bool laser_enabled  = (_received_data[7] != 0);

        mclog::info("yaw: {}, pitch: {}, speed: {}, laser: {}", yaw_angle, pitch_angle, speed, laser_enabled);

        auto& motion = GetStackChan().motion();
        motion.moveWithSpeed(yaw_angle, pitch_angle, speed);

        GetHAL().setLaserEnabled(laser_enabled);
    }

    _received_data.clear();
}

void handle_send_pose()
{
    static uint32_t last_send_tick = 0;

    if (GetHAL().millis() - last_send_tick < 50) {
        return;
    }
    last_send_tick = GetHAL().millis();

    // [target-id (uint8)] [yaw-angle (int16)] [pitch-angle (int16)] [speed (int16)] [laser-enabled (uint8)]
    // id: 0 for broadcast
    // yaw: -1280 ~ 1280
    // pitch: 0 ~ 900
    // speed: 0 ~ 1000, suggest 600
    // laser-enabled: 0 = off, 1 = on
    auto& motion        = GetStackChan().motion();
    int16_t yaw_angle   = motion.getCurrentYawAngle();
    int16_t pitch_angle = motion.getCurrentPitchAngle();
    const int16_t speed = 800;

    std::vector<uint8_t> data;
    data.reserve(8);  // 预留空间提高效率

    // [0] target-id
    data.push_back(_receiver_id);

    // [1-2] yaw-angle (小端序：先发低位，再发高位)
    data.push_back(static_cast<uint8_t>(yaw_angle & 0xFF));
    data.push_back(static_cast<uint8_t>((yaw_angle >> 8) & 0xFF));
    // [3-4] pitch-angle
    data.push_back(static_cast<uint8_t>(pitch_angle & 0xFF));
    data.push_back(static_cast<uint8_t>((pitch_angle >> 8) & 0xFF));
    // [5-6] speed
    data.push_back(static_cast<uint8_t>(speed & 0xFF));
    data.push_back(static_cast<uint8_t>((speed >> 8) & 0xFF));

    // [7] laser-enabled
    data.push_back(0);  // always off for sender

    GetHAL().espNowSend(data);
}

void AppEspnowControl::onRunning()
{
    LvglLockGuard lock;

    if (_is_receiver) {
        handle_received_data();
    } else {
        handle_send_pose();
    }

    GetStackChan().update();

    view::update_home_indicator();
    view::update_status_bar();
}

void AppEspnowControl::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");

    LvglLockGuard lock;

    view::destroy_home_indicator();
    view::destroy_status_bar();

    GetHAL().requestWarmReboot(2);
}
