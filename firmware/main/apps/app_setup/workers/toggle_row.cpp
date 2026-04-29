#include "toggle_row.h"
#include <cstring>

namespace setup_workers {

ToggleRow::ToggleRow(lv_obj_t* parent, const char* label_text)
    : _state(false)
{
    auto row = lv_obj_create(parent);
    lv_obj_set_size(row, 280, 28);
    lv_obj_add_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(row, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);

    auto label = lv_label_create(row);
    lv_label_set_text(label, label_text);
    lv_obj_add_flag(label, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(label, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_hex(0x26206A), LV_PART_MAIN);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);

    _sw = lv_switch_create(row);
    lv_obj_set_style_bg_color(_sw, lv_color_hex(0xB8D3FD), LV_PART_MAIN);
    lv_obj_set_style_bg_color(_sw, lv_color_hex(0xB8D3FD), LV_PART_INDICATOR);
}

void ToggleRow::set_state(bool enabled)
{
    _state = enabled;
    if (enabled) {
        lv_obj_add_state(_sw, LV_STATE_CHECKED);
    } else {
        lv_obj_remove_state(_sw, LV_STATE_CHECKED);
    }
}

bool ToggleRow::get_state() const
{
    return lv_obj_has_state(_sw, LV_STATE_CHECKED);
}

}  // namespace setup_workers