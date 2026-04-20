/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "workers.h"
#include <src/misc/lv_area.h>
#include <src/misc/lv_text.h>
#include <stackchan/stackchan.h>
#include <ArduinoJson.hpp>
#include <mooncake_log.h>
#include <hal/hal.h>
#include <memory>

using namespace smooth_ui_toolkit::lvgl_cpp;
using namespace setup_workers;
using namespace stackchan;

static std::string _tag = "Setup-Connectivity";

WifiSetupWorker::WifiSetupWorker()
{
    _state       = State::AppDownload;
    _last_state  = State::None;
    _is_first_in = true;

    // Create default avatar
    auto avatar = std::make_unique<avatar::DefaultAvatar>();
    avatar->init(lv_screen_active(), &lv_font_montserrat_24);
    avatar->leftEye().setVisible(false);
    avatar->rightEye().setVisible(false);
    avatar->mouth().setVisible(false);
    GetStackChan().attachAvatar(std::move(avatar));
}

WifiSetupWorker::~WifiSetupWorker()
{
    GetHAL().onAppConfigEvent.disconnect(_app_config_signal_id);
    GetStackChan().resetAvatar();
}

void WifiSetupWorker::update()
{
    cleanup_ui();
    update_state();
}

void WifiSetupWorker::update_state()
{
    switch (_state) {
        case State::AppDownload: {
            if (_is_first_in) {
                _is_first_in = false;

                auto& data = _state_app_download_data;

                data.panel = std::make_unique<Container>(lv_screen_active());
                data.panel->setBgColor(lv_color_hex(0xEDF4FF));
                data.panel->align(LV_ALIGN_CENTER, 0, 0);
                data.panel->setBorderWidth(0);
                data.panel->setSize(320, 240);
                data.panel->setRadius(0);

                data.title = std::make_unique<Label>(lv_screen_active());
                data.title->setTextFont(&lv_font_montserrat_20);
                data.title->setTextColor(lv_color_hex(0x7E7B9C));
                data.title->align(LV_ALIGN_TOP_MID, 0, 0);
                data.title->setText("APP SETUP");

                data.info = std::make_unique<Label>(lv_screen_active());
                data.info->setTextFont(&lv_font_montserrat_14);
                data.info->setTextColor(lv_color_hex(0x26206A));
                data.info->align(LV_ALIGN_TOP_MID, 0, 27);
                data.info->setTextAlign(LV_TEXT_ALIGN_CENTER);
                data.info->setText("Install \"StackChan World\" app\nand login to your M5Stack account");

                std::string qrcode_text = "https://apps.apple.com/us/app/stackchan-world/id6756086326";
                data.qrcode_ios         = std::make_unique<Qrcode>(lv_screen_active());
                data.qrcode_ios->setSize(80);
                data.qrcode_ios->setDarkColor(lv_color_hex(0x221C5B));
                data.qrcode_ios->setLightColor(lv_color_hex(0xEDF4FF));
                data.qrcode_ios->update(qrcode_text);
                data.qrcode_ios->align(LV_ALIGN_CENTER, -65, -12);

                qrcode_text         = "https://play.google.com/store/apps/details?id=com.m5stack.stackchan";
                data.qrcode_android = std::make_unique<Qrcode>(lv_screen_active());
                data.qrcode_android->setSize(80);
                data.qrcode_android->setDarkColor(lv_color_hex(0x221C5B));
                data.qrcode_android->setLightColor(lv_color_hex(0xEDF4FF));
                data.qrcode_android->update(qrcode_text);
                data.qrcode_android->align(LV_ALIGN_CENTER, 65, -12);

                data.label_ios = std::make_unique<Label>(lv_screen_active());
                data.label_ios->setTextFont(&lv_font_montserrat_14);
                data.label_ios->setTextColor(lv_color_hex(0x26206A));
                data.label_ios->align(LV_ALIGN_CENTER, -65, 47);
                data.label_ios->setText("App Store\n(iOS)");
                data.label_ios->setTextAlign(LV_TEXT_ALIGN_CENTER);

                data.label_android = std::make_unique<Label>(lv_screen_active());
                data.label_android->setTextFont(&lv_font_montserrat_14);
                data.label_android->setTextColor(lv_color_hex(0x26206A));
                data.label_android->align(LV_ALIGN_CENTER, 65, 47);
                data.label_android->setText("Play Store\n(Android)");
                data.label_android->setTextAlign(LV_TEXT_ALIGN_CENTER);

                data.btn_next = std::make_unique<Button>(lv_screen_active());
                apply_button_common_style(*data.btn_next);
                data.btn_next->align(LV_ALIGN_CENTER, 72, 91);
                data.btn_next->setSize(112, 42);
                data.btn_next->label().setText("Next");
                data.btn_next->onClick().connect([this]() { _state_app_download_data.next_clicked = true; });

                data.btn_quit = std::make_unique<Button>(lv_screen_active());
                apply_button_common_style(*data.btn_quit);
                data.btn_quit->align(LV_ALIGN_CENTER, -72, 91);
                data.btn_quit->setSize(112, 42);
                data.btn_quit->setBgColor(lv_color_hex(0xD4D9E0));
                data.btn_quit->label().setText("Back");
                data.btn_quit->label().setTextColor(lv_color_hex(0x525064));
                data.btn_quit->onClick().connect([this]() { _state_app_download_data.quit_clicked = true; });
            }

            if (_state_app_download_data.quit_clicked) {
                _is_done = true;
            }

            if (_state_app_download_data.next_clicked) {
                switch_state(State::WaitAppConnection);
            }

            // Check events
            if (_last_app_config_event != AppConfigEvent::None) {
                if (_last_app_config_event == AppConfigEvent::AppConnected) {
                    switch_state(State::AppConnected);
                }
                _last_app_config_event = AppConfigEvent::None;
            }

            break;
        }
        case State::WaitAppConnection: {
            if (_is_first_in) {
                _is_first_in = false;

                // Start app config server
                _app_config_signal_id =
                    GetHAL().onAppConfigEvent.connect([this](AppConfigEvent event) { _last_app_config_event = event; });

                GetHAL().startAppConfigServer();

                auto& data = _state_wait_app_connection_data;

                data.panel = std::make_unique<Container>(lv_screen_active());
                data.panel->setBgColor(lv_color_hex(0xEDF4FF));
                data.panel->align(LV_ALIGN_CENTER, 0, 0);
                data.panel->setBorderWidth(0);
                data.panel->setSize(320, 240);
                data.panel->setRadius(0);

                data.btn_id = std::make_unique<Button>(lv_screen_active());
                apply_button_common_style(*data.btn_id);
                data.btn_id->align(LV_ALIGN_CENTER, 0, -20);
                data.btn_id->setSize(262, 52);
                data.btn_id->onClick().connect([]() {
                    auto& avatar = GetStackChan().avatar();
                    avatar.clearDecorators();
                    avatar.addDecorator(std::make_unique<avatar::HeartDecorator>(lv_screen_active(), 3000));
                });
                data.btn_id->label().setText(fmt::format("ID: {}", GetHAL().getFactoryMacString()));

                data.info = std::make_unique<Label>(lv_screen_active());
                data.info->setTextFont(&lv_font_montserrat_24);
                data.info->setTextColor(lv_color_hex(0x26206A));
                data.info->align(LV_ALIGN_BOTTOM_MID, 0, -26);
                data.info->setTextAlign(LV_TEXT_ALIGN_CENTER);
                data.info->setText("Look for me in the app\nto start setup.");

                auto& avatar = GetStackChan().avatar();
                avatar.clearDecorators();
                avatar.addDecorator(std::make_unique<avatar::HeartDecorator>(lv_screen_active(), 3000));
            }

            // Check events
            if (_last_app_config_event != AppConfigEvent::None) {
                if (_last_app_config_event == AppConfigEvent::AppConnected) {
                    switch_state(State::AppConnected);
                }
                _last_app_config_event = AppConfigEvent::None;
            }

            break;
        }
        case State::AppConnected: {
            if (_is_first_in) {
                _is_first_in = false;

                auto& avatar = GetStackChan().avatar();
                avatar.leftEye().setVisible(true);
                avatar.rightEye().setVisible(true);
                avatar.mouth().setVisible(true);
                avatar.setSpeech("Ready to Configure ~");

                GetStackChan().addModifier(std::make_unique<TimedEmotionModifier>(avatar::Emotion::Happy, 4000));
                GetStackChan().addModifier(std::make_unique<BreathModifier>());
                GetStackChan().addModifier(std::make_unique<BlinkModifier>());
                GetStackChan().addModifier(std::make_unique<SpeakingModifier>(2000, 180, false));
            }

            // Check events
            if (_last_app_config_event != AppConfigEvent::None) {
                if (_last_app_config_event == AppConfigEvent::AppDisconnected) {
                    switch_state(State::WaitAppConnection);
                } else if (_last_app_config_event == AppConfigEvent::TryWifiConnect) {
                    auto& avatar = GetStackChan().avatar();
                    avatar.setSpeech("Verifying...");
                    GetStackChan().addModifier(std::make_unique<SpeakingModifier>(2000, 180, false));
                } else if (_last_app_config_event == AppConfigEvent::WifiConnectFailed) {
                    GetStackChan().addModifier(std::make_unique<TimedEmotionModifier>(avatar::Emotion::Sad, 4000));
                    GetStackChan().addModifier(
                        std::make_unique<TimedSpeechModifier>("Connect Failed. Try again?", 6000));
                    GetStackChan().addModifier(std::make_unique<SpeakingModifier>(3000, 180, false));
                } else if (_last_app_config_event == AppConfigEvent::WifiConnected) {
                    switch_state(State::Done);
                }
                _last_app_config_event = AppConfigEvent::None;
            }

            break;
        }
        case State::Done: {
            if (_is_first_in) {
                _is_first_in = false;

                auto& avatar = GetStackChan().avatar();
                avatar.leftEye().setVisible(true);
                avatar.rightEye().setVisible(true);
                avatar.mouth().setVisible(true);
                avatar.setEmotion(avatar::Emotion::Happy);

                GetStackChan().addModifier(std::make_unique<SpeakingModifier>(1500, 180, false));

                _state_done_data.reboot_count = 4;
            }

            if (GetHAL().millis() - _last_tick > 1000) {
                _last_tick = GetHAL().millis();
                if (_state_done_data.reboot_count > 0) {
                    _state_done_data.reboot_count--;
                    auto& avatar = GetStackChan().avatar();
                    avatar.setSpeech(fmt::format("Done!  Reboot in {}s.", _state_done_data.reboot_count));
                } else {
                    mclog::tagInfo(_tag, "rebooting...");
                    GetHAL().delay(100);
                    GetHAL().reboot();
                }
            }

            break;
        }
        default:
            break;
    }
}

void WifiSetupWorker::cleanup_ui()
{
    if (_last_state == State::None) {
        return;
    }

    switch (_last_state) {
        case State::AppDownload: {
            _state_app_download_data.reset();
            break;
        }
        case State::WaitAppConnection: {
            _state_wait_app_connection_data.reset();
            break;
        }
        case State::AppConnected: {
            GetStackChan().avatar().setSpeech("");
            GetStackChan().clearModifiers();
            break;
        }
        case State::Done: {
            break;
        }
        default:
            break;
    }

    _last_state = State::None;
}

void WifiSetupWorker::switch_state(State newState)
{
    _last_state  = _state;
    _state       = newState;
    _is_first_in = true;
}
