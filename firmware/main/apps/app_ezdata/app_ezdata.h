/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "view/view.h"
#include <mooncake.h>
#include <apps/common/common.h>

/**
 * @brief 派生 App
 *
 */
class AppEzdata : public mooncake::AppAbility {
public:
    AppEzdata();

    void onCreate() override;
    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    std::unique_ptr<view::LoadingPage> _loading_page;
    std::unique_ptr<view::EzdataGuidePage> _ezdata_guide_page;
};
