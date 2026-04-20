/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <mooncake.h>
#include <memory>
#include <mutex>

/**
 * @brief
 *
 */
class AppDance : public mooncake::AppAbility {
public:
    AppDance();

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
    BleHandlerData_t _ble_rgb_data;

    uint32_t _last_motion_cmd_tick = 0;

    void check_auto_angle_sync_mode();
};
