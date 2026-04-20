/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "view/view.h"
#include <mooncake.h>

/**
 * @brief 派生 App
 *
 */
class AppAppCenter : public mooncake::AppAbility {
public:
    AppAppCenter();

    // 重写生命周期回调
    void onCreate() override;
    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    std::vector<app_center::AppInfo_t> _app_list;
    std::unique_ptr<view::AppListPage> _app_list_page;
    std::unique_ptr<view::AppDetailPage> _app_detail_page;
    std::unique_ptr<view::AppInstallPage> _app_install_page;
    int _selected_index    = -1;
    bool _launch_requested = false;
};
