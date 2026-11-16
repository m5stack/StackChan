// Adapted from ThisIsBenny's StackChan PR #31.
#include "toggle_row.h"
#include <uitk/short_namespace.hpp>

using namespace uitk::lvgl_cpp;
using namespace setup_workers;

ToggleRow::ToggleRow(lv_obj_t* parent, const char* label_text)
{
    _row = std::make_unique<Container>(parent);
    _row->setSize(280, 40);
    _row->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
    _row->setFlexFlow(LV_FLEX_FLOW_ROW);
    _row->setFlexAlign(LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    _row->setBgOpa(0);
    _row->setBorderWidth(0);
    _row->setPadding(0, 0, 0, 0);

    _label = std::make_unique<Label>(_row->get());
    _label->setText(label_text);
    _label->setTextFont(&lv_font_montserrat_16);
    _label->setTextColor(lv_color_hex(0x26206A));

    _switch = std::make_unique<Switch>(_row->get());
    _switch->setSize(56, 30);
    _switch->setBgColor(lv_color_hex(0xB8D3FD), LV_PART_MAIN);
    _switch->setBgColor(lv_color_hex(0x615B9E), static_cast<lv_style_selector_t>(LV_PART_INDICATOR) | LV_STATE_CHECKED);
    _switch->setBgColor(lv_color_hex(0xFFFFFF), LV_PART_KNOB);
}

void ToggleRow::set_state(bool enabled)
{
    _switch->setValue(enabled);
}

bool ToggleRow::get_state() const
{
    return _switch->getValue();
}

void ToggleRow::set_editable(bool editable)
{
    if (editable)
        _switch->removeState(LV_STATE_DISABLED);
    else
        _switch->addState(LV_STATE_DISABLED);
}
