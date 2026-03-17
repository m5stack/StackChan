/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "view/video_window.hpp"
#include <mooncake.h>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

/**
 * @brief 派生 App
 *
 */
class AppAvatar : public mooncake::AppAbility {
public:
    AppAvatar();

    // 重写生命周期回调
    void onCreate() override;
    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    std::mutex _mutex;

    struct BleHandlerData_t {
        bool update_flag = false;
        char* data_ptr   = nullptr;
    };
    BleHandlerData_t _ble_avatar_data;
    BleHandlerData_t _ble_motion_data;

    int _ws_call_view_id = -1;

    uint32_t _last_motion_cmd_tick = 0;

    std::unique_ptr<view::VideoWindow> _video_window;

    void check_auto_angle_sync_mode();
};
