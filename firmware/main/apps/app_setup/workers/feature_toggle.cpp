#include "feature_toggle.h"
#include "toggle_row.h"
#include <mooncake_log.h>
#include <hal/hal.h>
#include "common.h"

using namespace setup_workers;
using namespace hal_toggles;

static const char* TAG = "FeatureToggle";

FeatureToggleWorker::FeatureToggleWorker()
{
    if (!hal_toggles::init_nvs()) {
        mclog::tagError(TAG, "NVS init failed");
        return;
    }

    for (int i = 0; i < TOGGLE_COUNT; i++) {
        _states[i] = hal_toggles::get_toggle_state(TOGGLE_LIST[i].key);
    }

    create_toggle_ui();
}

void FeatureToggleWorker::create_toggle_ui()
{
    auto screen = lv_screen_active();

    _panel = std::make_unique<uitk::lvgl_cpp::Container>(screen);
    _panel->setAlign(LV_ALIGN_CENTER);
    _panel->setPos(0, 0);
    _panel->setSize(320, 240);
    _panel->setBgColor(lv_color_hex(0xEDF4FF));
    _panel->setRadius(0);
    _panel->setBorderWidth(0);
    _panel->setFlexFlow(LV_FLEX_FLOW_COLUMN);
    _panel->setFlexAlign(LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    _panel->setPadding(15, 20, 10, 20);
    _panel->setPadRow(10);

    _title = std::make_unique<uitk::lvgl_cpp::Label>(_panel->get());
    _title->setText("Feature Toggles");
    _title->setTextFont(&lv_font_montserrat_20);
    _title->setTextColor(lv_color_hex(0x26206A));
    _title->setTextAlign(LV_TEXT_ALIGN_CENTER);

    _panel->setScrollbarMode(LV_SCROLLBAR_MODE_OFF);

    for (int i = 0; i < TOGGLE_COUNT; i++) {
        auto row = std::make_unique<ToggleRow>(_panel->get(), TOGGLE_LIST[i].display_name);
        row->set_state(_states[i]);
        _toggle_rows_widgets.push_back(std::move(row));
    }

    _reset_button = std::make_unique<uitk::lvgl_cpp::Button>(_panel->get());
    _reset_button->setSize(260, 40);
    apply_button_common_style(*_reset_button);
    _reset_button->label().setText("Reset");
    _reset_button->onClick().connect([this]() {
        hal_toggles::reset_toggles_to_defaults();
        for (int i = 0; i < TOGGLE_COUNT; i++) {
            _states[i] = true;
            _toggle_rows_widgets[i]->set_state(true);
        }
        mclog::tagInfo(TAG, "Toggles reset to defaults");
    });

    _back_button = std::make_unique<uitk::lvgl_cpp::Button>(_panel->get());
    _back_button->setSize(260, 40);
    apply_button_common_style(*_back_button);
    _back_button->label().setText("Back");
    _back_button->onClick().connect([this]() {
        _is_done = true;
    });
}

void FeatureToggleWorker::update()
{
    for (int i = 0; i < TOGGLE_COUNT; i++) {
        bool current_state = _toggle_rows_widgets[i]->get_state();
        if (current_state != _states[i]) {
            _states[i] = current_state;
            hal_toggles::set_toggle_state(TOGGLE_LIST[i].key, current_state);
            mclog::tagInfo(TAG, "Toggle %s = %s", TOGGLE_LIST[i].display_name, current_state ? "ON" : "OFF");
        }
    }
}