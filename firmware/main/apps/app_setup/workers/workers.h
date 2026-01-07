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
    std::unique_ptr<uitk::lvgl_cpp::Container> _pannel;
    std::unique_ptr<uitk::lvgl_cpp::Button> _btn_quit;
    std::unique_ptr<uitk::lvgl_cpp::Button> _btn_confirm;
    std::unique_ptr<uitk::lvgl_cpp::Button> _btn_go_home;
    bool _confirm_flag = false;
    bool _go_home_flag = false;
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
        std::unique_ptr<uitk::lvgl_cpp::Qrcode> qrcode;
        std::unique_ptr<uitk::lvgl_cpp::Button> btn;
        bool is_clicked = false;

        void reset()
        {
            qrcode.reset();
            btn.reset();
            is_clicked = false;
        }
    };
    StateAppDownloadData _state_app_download_data;

    struct StateWaitAppConnectionData {
        std::unique_ptr<uitk::lvgl_cpp::Button> id_btn;

        void reset()
        {
            id_btn.reset();
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
class AppBindCodeWorker : public WorkerBase {
public:
    AppBindCodeWorker();
    ~AppBindCodeWorker();

private:
    std::unique_ptr<uitk::lvgl_cpp::Qrcode> _qrcode;
    std::unique_ptr<uitk::lvgl_cpp::Button> _btn_quit;
};

class RgbTestWorker : public WorkerBase {
public:
    RgbTestWorker();
    ~RgbTestWorker();

private:
    std::unique_ptr<uitk::lvgl_cpp::Container> _pannel;
    std::vector<std::unique_ptr<uitk::lvgl_cpp::Button>> _buttons;
};

}  // namespace setup_workers
