/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_espnow_ctrl.h"
#include "view/page_selector.h"
#include <hal/hal.h>
#include <mooncake.h>
#include <mooncake_log.h>
#include <assets/assets.h>
#include <smooth_lvgl.hpp>
#include <stackchan/stackchan.h>
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
    static uint32_t theme_color = 0xCCCC33;
    setAppInfo().userData       = (void*)&theme_color;
}

void AppEspnowControl::onCreate()
{
    mclog::tagInfo(getAppInfo().name, "on create");
}

static std::mutex _mutex;
static std::vector<uint8_t> _received_data;
static int _receiver_id  = 0;
static bool _is_receiver = false;

void AppEspnowControl::onOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");

    // Get role
    std::vector<std::string> role_options = {"Receiver", "Sender"};
    int role_selection                    = view::create_page_selector_and_wait("Select Role", role_options);
    _is_receiver                          = (role_selection == 0);
    mclog::tagInfo(getAppInfo().name, "selected role: {}", _is_receiver ? "Receiver" : "Sender");

    // Get channel
    std::vector<std::string> channel_options;
    for (int i = 0; i < 13; i++) {
        channel_options.push_back(std::to_string(i + 1));
    }
    auto wifi_channel = view::create_page_selector_and_wait("Select WiFi Channel", channel_options) + 1;
    mclog::tagInfo(getAppInfo().name, "selected wifi channel: {}", wifi_channel);

    // Get id
    std::vector<std::string> id_options;
    for (int i = 0; i < 255; i++) {
        id_options.push_back(std::to_string(i));
    }
    _receiver_id = view::create_page_selector_and_wait("Select ID", id_options);
    mclog::tagInfo(getAppInfo().name, "selected id: {}", _receiver_id);

    // Start espnow
    GetHAL().startEspNow(wifi_channel);
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

    stackchan.addModifier(std::make_unique<BreathModifier>());
    stackchan.addModifier(std::make_unique<BlinkModifier>());

    // Diable auto angle sync to prevent jitter
    stackchan.motion().setAutoAngleSyncEnabled(false);

    GetHAL().setLaserEnabled(false);
}

void handle_reived_data()
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
        handle_reived_data();
    } else {
        handle_send_pose();
    }

    GetStackChan().update();
}

void AppEspnowControl::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");

    LvglLockGuard lock;
}
