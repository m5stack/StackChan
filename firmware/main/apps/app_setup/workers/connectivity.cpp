/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "workers.h"
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
    GetStackChan().attachAvatar(std::move(avatar));

    _app_config_signal_id =
        GetHAL().onAppConfigEvent.connect([this](AppConfigEvent event) { _last_app_config_event = event; });

    GetHAL().startAppConfigServer();
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

                auto& avatar = GetStackChan().avatar();
                avatar.leftEye().setVisible(false);
                avatar.rightEye().setVisible(false);
                avatar.mouth().setVisible(false);
                avatar.setSpeech("Scan the QR code to download the \"StackChan\" app.");

                auto& data = _state_app_download_data;

                std::string qrcode_text = "todotodotodotodotodotodotodlotodotodotodoto";
                data.qrcode             = std::make_unique<Qrcode>(lv_screen_active());
                data.qrcode->setSize(150);
                data.qrcode->setDarkColor(lv_color_white());
                data.qrcode->setLightColor(lv_color_black());
                data.qrcode->update(qrcode_text);
                data.qrcode->align(LV_ALIGN_CENTER, -60, -25);

                data.btn = std::make_unique<Button>(lv_screen_active());
                apply_button_common_style(*data.btn);
                data.btn->align(LV_ALIGN_CENTER, 90, 0);
                data.btn->setSize(100, 60);
                data.btn->label().setText("Next");
                data.btn->onClick().connect([this]() { _state_app_download_data.is_clicked = true; });
            }

            if (_state_app_download_data.is_clicked) {
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

                auto& avatar = GetStackChan().avatar();
                avatar.leftEye().setVisible(false);
                avatar.rightEye().setVisible(false);
                avatar.mouth().setVisible(false);
                avatar.setSpeech("Look for me in \"StackChan\" app to start the setup.");
                avatar.addDecorator(std::make_unique<avatar::HeartDecorator>(lv_screen_active(), 3000));

                auto& data  = _state_wait_app_connection_data;
                data.id_btn = std::make_unique<Button>(lv_screen_active());
                apply_button_common_style(*data.id_btn);
                data.id_btn->align(LV_ALIGN_CENTER, 0, 0);
                data.id_btn->setSize(262, 52);
                data.id_btn->onClick().connect([]() {
                    auto& avatar = GetStackChan().avatar();
                    avatar.clearDecorators();
                    avatar.addDecorator(std::make_unique<avatar::HeartDecorator>(lv_screen_active(), 3000));
                });
                data.id_btn->label().setText(fmt::format("ID: {}", GetHAL().getFactoryMacString()));
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
            GetStackChan().avatar().setSpeech("");
            break;
        }
        case State::WaitAppConnection: {
            _state_wait_app_connection_data.reset();
            GetStackChan().avatar().setSpeech("");
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

AppBindCodeWorker::AppBindCodeWorker()
{
    // Create default avatar
    auto avatar = std::make_unique<avatar::DefaultAvatar>();
    avatar->init(lv_screen_active(), &lv_font_montserrat_24);
    avatar->leftEye().setVisible(false);
    avatar->rightEye().setVisible(false);
    avatar->mouth().setVisible(false);
    avatar->setSpeech("Scan the QR code with the StackChan APP to bind.");
    GetStackChan().attachAvatar(std::move(avatar));

    std::string qrcode_text;
    ArduinoJson::JsonDocument doc;
    doc["mac"] = GetHAL().getFactoryMacString();
    ArduinoJson::serializeJson(doc, qrcode_text);
    mclog::tagInfo(_tag, "qr code text: {}", qrcode_text);

    _qrcode = std::make_unique<Qrcode>(lv_screen_active());
    _qrcode->setSize(150);
    _qrcode->setDarkColor(lv_color_white());
    _qrcode->setLightColor(lv_color_black());
    _qrcode->update(qrcode_text);
    _qrcode->align(LV_ALIGN_CENTER, -60, -25);

    _btn_quit = std::make_unique<Button>(lv_screen_active());
    apply_button_common_style(*_btn_quit);
    _btn_quit->align(LV_ALIGN_CENTER, 90, 0);
    _btn_quit->setSize(100, 60);
    _btn_quit->label().setText("Back");
    _btn_quit->onClick().connect([this]() { _is_done = true; });
}

AppBindCodeWorker::~AppBindCodeWorker()
{
    GetStackChan().resetAvatar();
}
