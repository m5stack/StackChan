#pragma once
#include <smooth_lvgl.hpp>
#include <lvgl.h>

namespace setup_workers {

class ToggleRow {
public:
    ToggleRow(lv_obj_t* parent, const char* label_text);
    void set_state(bool enabled);
    bool get_state() const;
    lv_obj_t* get_switch() const { return _sw; }

private:
    lv_obj_t* _sw;
    bool _state;
};

}  // namespace setup_workers