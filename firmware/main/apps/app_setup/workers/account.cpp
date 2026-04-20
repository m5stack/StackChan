/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "workers.h"
#include <hal/hal.h>
#include <mooncake_log.h>
#include <apps/common/loading_page/loading_page.h>

using namespace uitk::lvgl_cpp;
using namespace setup_workers;

static std::string _tag = "Setup-Account";

AccountWorker::PanelInfo::PanelInfo(lv_obj_t* parent, int posY, std::string_view title, std::string_view info)
{
    _panel = std::make_unique<Container>(parent);
    _panel->setBgColor(lv_color_hex(0xB8D3FD));
    _panel->align(LV_ALIGN_TOP_MID, 0, posY);
    _panel->setBorderWidth(0);
    _panel->setSize(296, 108);
    _panel->setPadding(0, 0, 0, 0);
    _panel->setRadius(18);
    _panel->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

    _label_title = std::make_unique<Label>(_panel->get());
    _label_title->setTextFont(&lv_font_montserrat_20);
    _label_title->setTextColor(lv_color_hex(0x445B80));
    _label_title->align(LV_ALIGN_TOP_MID, 0, 13);
    _label_title->setText(title);

    _label_info = std::make_unique<Label>(_panel->get());
    _label_info->setTextFont(&lv_font_montserrat_20);
    _label_info->setTextColor(lv_color_hex(0x07162C));
    _label_info->align(LV_ALIGN_CENTER, 0, 14);
    _label_info->setWidth(270);
    _label_info->setTextAlign(LV_TEXT_ALIGN_CENTER);
    _label_info->setText(info);
}

AccountWorker::PageAccount::PageAccount(std::string_view username, std::string_view deviceName)
{
    _panel = std::make_unique<Container>(lv_screen_active());
    _panel->setBgColor(lv_color_hex(0xF6F6F6));
    _panel->align(LV_ALIGN_CENTER, 0, 0);
    _panel->setBorderWidth(0);
    _panel->setSize(320, 240);
    _panel->setPadding(0, 20, 0, 0);
    _panel->setRadius(0);

    // Title
    _label_title = std::make_unique<Label>(_panel->get());
    _label_title->setTextFont(&lv_font_montserrat_20);
    _label_title->setTextColor(lv_color_hex(0x7E7B9C));
    _label_title->align(LV_ALIGN_TOP_MID, 0, 12);
    _label_title->setText("ACCOUNT");

    _panel_username    = std::make_unique<PanelInfo>(_panel->get(), 50, "M5Stack Account:", username);
    _panel_device_name = std::make_unique<PanelInfo>(_panel->get(), 174, "Device Name:", deviceName);

    // Button
    _btn_unbind = std::make_unique<Button>(_panel->get());
    apply_button_common_style(*_btn_unbind);
    _btn_unbind->align(LV_ALIGN_TOP_MID, 0, 303);
    _btn_unbind->setSize(290, 48);
    _btn_unbind->setBgColor(lv_color_hex(0xFF8080));
    _btn_unbind->label().setText("Unbind and factory reset");
    _btn_unbind->label().setTextFont(&lv_font_montserrat_20);
    _btn_unbind->label().setTextColor(lv_color_hex(0x731F1F));
    _btn_unbind->onClick().connect([this]() { _is_unbind_clicked = true; });

    _btn_quit = std::make_unique<Button>(_panel->get());
    apply_button_common_style(*_btn_quit);
    _btn_quit->align(LV_ALIGN_TOP_MID, 0, 371);
    _btn_quit->setSize(290, 48);
    _btn_quit->label().setText("Back");
    _btn_quit->label().setTextFont(&lv_font_montserrat_20);
    _btn_quit->onClick().connect([this]() { _is_quit_clicked = true; });
}

AccountWorker::AccountWorker()
{
    // Update account info
    {
        auto loading_page = std::make_unique<view::LoadingPage>(0xF6F6F6, 0x26206A);
        GetHAL().lvglUnlock();

        // Start network
        GetHAL().startNetwork([&](std::string_view msg) {
            LvglLockGuard lock;
            loading_page->setMessage(msg);
        });

        // Update info
        bool result = GetHAL().updateAccountInfo([&](std::string_view msg) {
            LvglLockGuard lock;
            loading_page->setMessage(msg);
        });

        if (!result) {
            GetHAL().delay(5000);
        }

        GetHAL().lvglLock();
    }

    auto info     = GetHAL().getUserAccountInfo();
    _page_account = std::make_unique<PageAccount>(info.username, info.deviceName);
}

AccountWorker::~AccountWorker()
{
}

void AccountWorker::update()
{
    if (_page_account) {
        if (_page_account->isUnbindClicked()) {
            mclog::tagInfo(_tag, "unbind clicked");
            _page_account.reset();

            // Unbind account
            {
                auto loading_page = std::make_unique<view::LoadingPage>(0xF6F6F6, 0x26206A);
                GetHAL().lvglUnlock();

                bool result = GetHAL().unbindAccount([&](std::string_view msg) {
                    LvglLockGuard lock;
                    loading_page->setMessage(msg);
                });

                if (!result) {
                    GetHAL().delay(5000);
                }

                GetHAL().lvglLock();
            }

            // Factory reset
            _worker_reset = std::make_unique<FactoryResetWorker>();
        } else if (_page_account->isQuitClicked()) {
            mclog::tagInfo(_tag, "quit clicked");
            _is_done = true;
        }
    } else if (_worker_reset) {
        _worker_reset->update();
        if (_worker_reset->isDone()) {
            _worker_reset.reset();
            _is_done = true;
        }
    }
}
