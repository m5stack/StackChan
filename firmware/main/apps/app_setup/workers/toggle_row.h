// Adapted from ThisIsBenny's StackChan PR #31.
#pragma once
#include <smooth_lvgl.hpp>
#include <uitk/short_namespace.hpp>
#include <memory>

namespace setup_workers {
class ToggleRow {
public:
    ToggleRow(lv_obj_t* parent, const char* label_text);
    void set_state(bool enabled);
    bool get_state() const;
    void set_editable(bool editable);

private:
    std::unique_ptr<uitk::lvgl_cpp::Container> _row;
    std::unique_ptr<uitk::lvgl_cpp::Label> _label;
    std::unique_ptr<uitk::lvgl_cpp::Switch> _switch;
};
}  // namespace setup_workers
