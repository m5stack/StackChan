/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <mooncake.h>

class AppAtlasAgent : public mooncake::AppAbility {
public:
    AppAtlasAgent();

    void onCreate() override;
    void onOpen() override;
    void onRunning() override;
    void onClose() override;
};
