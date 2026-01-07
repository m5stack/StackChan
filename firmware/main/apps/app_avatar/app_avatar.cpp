/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_avatar.h"
#include "view/ws_call.h"
#include <hal/hal.h>
#include <mooncake.h>
#include <mooncake_log.h>
#include <assets/assets.h>
#include <smooth_lvgl.hpp>
#include <stackchan/stackchan.h>
#include <string_view>
#include <cstdint>
#include <memory>

using namespace mooncake;
using namespace smooth_ui_toolkit::lvgl_cpp;
using namespace stackchan;

#include <string>
#include <sstream>
#include <unordered_set>

static bool contains_word(const std::string& text, const std::unordered_set<std::string>& words)
{
    auto to_lower = [](std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
        return s;
    };

    std::istringstream iss(text);
    std::string token;
    while (iss >> token) {
        token = to_lower(token);
        if (words.find(token) != words.end()) {
            return true;
        }
    }
    return false;
}

AppAvatar::AppAvatar()
{
    // 配置 App 名
    setAppInfo().name = "AVATAR";
    // 配置 App 图标
    setAppInfo().icon = (void*)&icon_sentinel;
    // 配置 App 主题颜色
    static uint32_t theme_color = 0xFF6699;
    setAppInfo().userData       = (void*)&theme_color;
}

// App 被安装时会被调用
void AppAvatar::onCreate()
{
    mclog::tagInfo(getAppInfo().name, "on create");
}

void AppAvatar::onOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");

    // GetHAL().startBleServer();
    GetHAL().startWebSocketAvatar();

    LvglLockGuard lock;

    // Create default avatar
    auto avatar = std::make_unique<avatar::DefaultAvatar>();
    avatar->init(lv_screen_active());
    GetStackChan().attachAvatar(std::move(avatar));

    /* ------------------------------- BLE events ------------------------------- */
    _ble_avatar_data.callback_id = GetHAL().onBleAvatarData.connect([&](const char* data) {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_ble_avatar_data.update_flag) {
            return;
        }
        _ble_avatar_data.update_flag = true;
        _ble_avatar_data.data_ptr    = (char*)data;
    });

    _ble_motion_data.callback_id = GetHAL().onBleMotionData.connect([&](const char* data) {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_ble_motion_data.update_flag) {
            return;
        }
        _ble_motion_data.update_flag = true;
        _ble_motion_data.data_ptr    = (char*)data;
    });

    /* ---------------------------- Websocket events ---------------------------- */
    // Avatar control
    _ws_callback_ids.avatar_id = GetHAL().onWsAvatarData.connect([&](std::string_view data) {
        LvglLockGuard lvgl_lock;
        GetStackChan().updateAvatarFromJson(data.data());
    });

    // Motion control
    _ws_callback_ids.motion_id = GetHAL().onWsMotionData.connect([&](std::string_view data) {
        LvglLockGuard lvgl_lock;
        check_auto_angle_sync_mode();
        GetStackChan().updateMotionFromJson(data.data());
    });

    // Phone call handling
    _ws_callback_ids.call_req_id = GetHAL().onWsCallRequest.connect([&](std::string caller) {
        if (_ws_call_view_id >= 0) {
            mclog::tagWarn(getAppInfo().name, "ws call view already exists");
            return;
        }

        LvglLockGuard lvgl_lock;

        auto& avatar = GetStackChan().avatar();
        avatar.setSpeech("");
        avatar.leftEye().setVisible(false);
        avatar.rightEye().setVisible(false);
        avatar.mouth().setVisible(false);

        auto view      = std::make_unique<view::WsCallView>(lv_screen_active(), caller);
        view->onAccept = []() {
            auto& avatar = GetStackChan().avatar();
            avatar.setSpeech("");
            avatar.leftEye().setVisible(true);
            avatar.rightEye().setVisible(true);
            avatar.mouth().setVisible(true);

            GetHAL().onWsCallResponse.emit(true);
        };
        view->onDecline = []() {
            auto& avatar = GetStackChan().avatar();
            avatar.setSpeech("");
            avatar.leftEye().setVisible(true);
            avatar.rightEye().setVisible(true);
            avatar.mouth().setVisible(true);

            GetHAL().onWsCallResponse.emit(false);
        };
        view->onEnd     = []() { GetHAL().onWsCallEnd.emit(WsSignalSource::Local); };
        view->onDestory = [&]() { _ws_call_view_id = -1; };

        _ws_call_view_id = avatar.addDecorator(std::move(view));
    });

    _ws_callback_ids.call_end_id = GetHAL().onWsCallEnd.connect([&](WsSignalSource source) {
        if (source != WsSignalSource::Remote) {
            return;
        }

        LvglLockGuard lvgl_lock;

        if (_ws_call_view_id < 0) {
            mclog::tagWarn(getAppInfo().name, "ws call view does not exist");
            return;
        }

        auto& avatar = GetStackChan().avatar();
        avatar.setSpeech("");
        avatar.leftEye().setVisible(true);
        avatar.rightEye().setVisible(true);
        avatar.mouth().setVisible(true);

        avatar.removeDecorator(_ws_call_view_id);
        _ws_call_view_id = -1;
    });

    // Text message handling
    _ws_callback_ids.text_msg_id = GetHAL().onWsTextMessage.connect([&](const WsTextMessage_t& message) {
        LvglLockGuard lvgl_lock;

        auto& stackchan = GetStackChan();
        auto& avatar    = stackchan.avatar();

        stackchan.addModifier(
            std::make_unique<TimedSpeechModifier>(fmt::format("{} says: {}", message.name, message.content), 6000));
        stackchan.addModifier(std::make_unique<SpeakingModifier>(2000));

        // Special handling
        if (contains_word(message.content, {"hello", "hi"})) {
            stackchan.addModifier(std::make_unique<TimedEmotionModifier>(avatar::Emotion::Happy, 2000));
        } else if (contains_word(message.content, {"love"})) {
            stackchan.addModifier(std::make_unique<TimedEmotionModifier>(avatar::Emotion::Happy, 2000));
        }
    });

    _ws_callback_ids.dance_data_id = GetHAL().onWsDanceData.connect([&](std::string_view data) {
        LvglLockGuard lvgl_lock;
        auto sequence = stackchan::animation::parse_sequence_from_json(data.data());
        if (!sequence.empty()) {
            GetStackChan().addModifier(std::make_unique<DanceModifier>(sequence));
        }
    });

    /* ------------------------------ Video window ------------------------------ */
    _video_window = std::make_unique<view::VideoWindow>(lv_screen_active());
}

void AppAvatar::onRunning()
{
    std::lock_guard<std::mutex> lock(_mutex);

    LvglLockGuard lvgl_lock;

    if (_ble_avatar_data.update_flag) {
        GetStackChan().updateAvatarFromJson(_ble_avatar_data.data_ptr);
        _ble_avatar_data.update_flag = false;
        _ble_avatar_data.data_ptr    = nullptr;
    }

    if (_ble_motion_data.update_flag) {
        check_auto_angle_sync_mode();
        GetStackChan().updateMotionFromJson(_ble_motion_data.data_ptr);
        _ble_motion_data.update_flag = false;
        _ble_motion_data.data_ptr    = nullptr;
    }

    GetStackChan().update();
}

void AppAvatar::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");

    LvglLockGuard lock;

    GetStackChan().resetAvatar();
    _video_window.reset();

    GetHAL().onBleAvatarData.disconnect(_ble_avatar_data.callback_id);
    GetHAL().onBleMotionData.disconnect(_ble_motion_data.callback_id);

    GetHAL().onWsAvatarData.disconnect(_ws_callback_ids.avatar_id);
    GetHAL().onWsMotionData.disconnect(_ws_callback_ids.motion_id);
    GetHAL().onWsCallRequest.disconnect(_ws_callback_ids.call_req_id);
    GetHAL().onWsCallEnd.disconnect(_ws_callback_ids.call_end_id);
    GetHAL().onWsTextMessage.disconnect(_ws_callback_ids.text_msg_id);
    GetHAL().onWsDanceData.disconnect(_ws_callback_ids.dance_data_id);
}

void AppAvatar::check_auto_angle_sync_mode()
{
    auto& motion = GetStackChan().motion();

    // If far from last command, enable auto angle sync
    if (GetHAL().millis() - _last_motion_cmd_tick > 2000) {
        motion.setAutoAngleSyncEnabled(true);
    } else {
        motion.setAutoAngleSyncEnabled(false);
    }

    _last_motion_cmd_tick = GetHAL().millis();
}
