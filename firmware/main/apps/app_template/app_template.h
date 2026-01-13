/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <mooncake.h>

/**
 * @brief 派生 App
 *
 */
class AppTemplate : public mooncake::AppAbility {
public:
    AppTemplate();

    // 重写生命周期回调
    void onCreate() override;
    void onOpen() override;
    void onRunning() override;
    void onClose() override;
};
