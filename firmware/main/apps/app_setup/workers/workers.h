/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "common.h"
#include <smooth_lvgl.hpp>
#include <uitk/short_namespace.hpp>
#include <hal/hal.h>
#include <cstdint>
#include <memory>

namespace setup_workers {

/**
 * @brief
 *
 */
class WorkerBase {
public:
    virtual ~WorkerBase() = default;

    virtual void update()
    {
    }

    bool isDone() const
    {
        return _is_done;
    }

protected:
    bool _is_done = false;
};

/**
 * @brief
 *
 */
class ZeroCalibrationWorker : public WorkerBase {
public:
    ZeroCalibrationWorker();
    void update() override;

private:
    std::unique_ptr<WorkerBase> _page_tips;
    std::unique_ptr<WorkerBase> _page_calibration;
};

/**
 * @brief
 *
 */
class WifiSetupWorker : public WorkerBase {
public:
    WifiSetupWorker();
    ~WifiSetupWorker();
    void update() override;

private:
    enum class State {
        None,
        AppDownload,
        WaitAppConnection,
        AppConnected,
        Done,
    };

    State _state      = State::AppDownload;
    State _last_state = State::None;

    uint32_t _last_tick = 0;
    bool _is_first_in   = false;

    AppConfigEvent _last_app_config_event = AppConfigEvent::None;
    int _app_config_signal_id             = -1;

    struct StateAppDownloadData {
        std::unique_ptr<uitk::lvgl_cpp::Container> panel;
        std::unique_ptr<uitk::lvgl_cpp::Label> title;
        std::unique_ptr<uitk::lvgl_cpp::Qrcode> qrcode;
        std::unique_ptr<uitk::lvgl_cpp::Button> btn_next;
        std::unique_ptr<uitk::lvgl_cpp::Label> info;
        bool next_clicked = false;

        void reset()
        {
            panel.reset();
            title.reset();
            qrcode.reset();
            btn_next.reset();
            info.reset();
            next_clicked = false;
        }
    };
    StateAppDownloadData _state_app_download_data;

    struct StateWaitAppConnectionData {
        std::unique_ptr<uitk::lvgl_cpp::Container> panel;
        std::unique_ptr<uitk::lvgl_cpp::Button> btn_id;
        std::unique_ptr<uitk::lvgl_cpp::Label> info;

        void reset()
        {
            panel.reset();
            btn_id.reset();
            info.reset();
        }
    };
    StateWaitAppConnectionData _state_wait_app_connection_data;

    struct StateDoneData {
        int reboot_count = 0;
    };
    StateDoneData _state_done_data;

    void update_state();
    void cleanup_ui();
    void switch_state(State newState);
};

/**
 * @brief
 *
 */
class RgbTestWorker : public WorkerBase {
public:
    RgbTestWorker();
    ~RgbTestWorker();

private:
    std::unique_ptr<uitk::lvgl_cpp::Container> _panel;
    std::vector<std::unique_ptr<uitk::lvgl_cpp::Button>> _buttons;
};

/**
 * @brief
 *
 */
class StartupWorker : public WorkerBase {
public:
    class PageStartup {
    public:
        PageStartup();

        bool isSkipClicked() const
        {
            return _is_skip_clicked;
        }

        bool isStartClicked() const
        {
            return _is_start_clicked;
        }

    private:
        std::unique_ptr<uitk::lvgl_cpp::Container> _panel;
        std::unique_ptr<uitk::lvgl_cpp::Label> _info;
        std::unique_ptr<uitk::lvgl_cpp::Button> _btn_skip;
        std::unique_ptr<uitk::lvgl_cpp::Button> _btn_start;

        bool _is_skip_clicked  = false;
        bool _is_start_clicked = false;
    };

    StartupWorker();
    ~StartupWorker();
    void update() override;

private:
    std::unique_ptr<PageStartup> _page_startup;
    std::unique_ptr<WifiSetupWorker> _worker_wifi;
};

/**
 * @brief
 *
 */
class FwVersionWorker : public WorkerBase {
public:
    FwVersionWorker();
    ~FwVersionWorker();
    void update() override;

private:
    uint32_t _last_tick = 0;
};

/**
 * @brief
 *
 */
class BrightnessSetupWorker : public WorkerBase {
public:
    BrightnessSetupWorker();
    ~BrightnessSetupWorker();
    void update() override;

private:
    std::unique_ptr<uitk::lvgl_cpp::Container> _panel;
    std::unique_ptr<uitk::lvgl_cpp::Label> _label_brightness;
    std::unique_ptr<uitk::lvgl_cpp::Slider> _slider;
    std::unique_ptr<uitk::lvgl_cpp::Button> _btn_confirm;
    int32_t _target_brightness = -1;
};

/**
 * @brief
 *
 */
class TimezoneWorker : public WorkerBase {
public:
    TimezoneWorker();
    ~TimezoneWorker();
    void update() override;

private:
    std::unique_ptr<uitk::lvgl_cpp::Container> _panel;
    std::unique_ptr<uitk::lvgl_cpp::Roller> _roller;
    std::unique_ptr<uitk::lvgl_cpp::Button> _btn_confirm;
    std::unique_ptr<uitk::lvgl_cpp::Label> _label;
    bool _confirm_flag = false;
};

/**
 * @brief
 *
 */
class FactoryResetWorker : public WorkerBase {
public:
    FactoryResetWorker();
    ~FactoryResetWorker();
    void update() override;

private:
    std::unique_ptr<uitk::lvgl_cpp::Container> _panel;
    std::unique_ptr<uitk::lvgl_cpp::Label> _label_title;
    std::unique_ptr<uitk::lvgl_cpp::Label> _label_info;
    std::unique_ptr<uitk::lvgl_cpp::Button> _btn_cancel;
    std::unique_ptr<uitk::lvgl_cpp::Button> _btn_confirm;

    int _confirm_count = 0;
    bool _cancel_flag  = false;
    bool _confirm_flag = false;

    void update_ui();
};

}  // namespace setup_workers
