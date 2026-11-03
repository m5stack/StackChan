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
    void update_toggle_display();

    std::unique_ptr<uitk::lvgl_cpp::Container> _panel;
    std::unique_ptr<uitk::lvgl_cpp::Label> _title;
    std::vector<std::unique_ptr<ToggleRow>> _toggle_rows_widgets;
    std::unique_ptr<uitk::lvgl_cpp::Button> _reset_button;
    std::unique_ptr<uitk::lvgl_cpp::Button> _back_button;

    bool _states[hal_toggles::TOGGLE_COUNT];
};

}  // namespace setup_workers