// Adapted from ThisIsBenny's StackChan PR #31.
#pragma once
#include "workers.h"
#include "toggle_row.h"
#include <hal/hal_toggles.h>

namespace setup_workers {
class FeatureToggleWorker : public WorkerBase {
public:
    FeatureToggleWorker();
    void update() override;

private:
    void create_toggle_ui();
    void refresh_rows();
    void load_preferences();
    bool save_preferences(const hal_toggles::ToggleStates& desired);

    std::unique_ptr<uitk::lvgl_cpp::Container> _panel;
    std::unique_ptr<uitk::lvgl_cpp::Label> _title, _status;
    std::vector<std::unique_ptr<ToggleRow>> _toggle_rows_widgets;
    std::unique_ptr<uitk::lvgl_cpp::Button> _reset_button, _back_button;
    hal_toggles::ToggleStates _states = hal_toggles::default_states();
    bool _loaded                      = false;
    bool _reset_requested             = false;
};
}  // namespace setup_workers
