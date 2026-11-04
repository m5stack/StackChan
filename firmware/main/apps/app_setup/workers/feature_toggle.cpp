// Adapted from ThisIsBenny's StackChan PR #31.
#include "feature_toggle.h"
#include "common.h"
#include <mooncake_log.h>

using namespace setup_workers;
using namespace uitk::lvgl_cpp;
using namespace hal_toggles;

namespace {
constexpr const char* TAG  = "AppVisibility";
constexpr const char* HELP = "Only menu icons change.\nAI auto-start is set separately.";
}  // namespace

FeatureToggleWorker::FeatureToggleWorker()
{
    // Always build Back / Retry controls, even when preferences cannot load.
    create_toggle_ui();
    load_preferences();
}

void FeatureToggleWorker::create_toggle_ui()
{
    _panel = std::make_unique<Container>(lv_screen_active());
    _panel->setAlign(LV_ALIGN_CENTER);
    _panel->setSize(320, 240);
    _panel->setBgColor(lv_color_hex(0xEDF4FF));
    _panel->setRadius(0);
    _panel->setBorderWidth(0);
    _panel->setFlexFlow(LV_FLEX_FLOW_COLUMN);
    _panel->setFlexAlign(LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    _panel->setPadding(16, 16, 16, 16);
    _panel->setPadRow(12);
    _panel->setScrollDir(LV_DIR_VER);
    _panel->setScrollbarMode(LV_SCROLLBAR_MODE_ACTIVE);

    _title = std::make_unique<Label>(_panel->get());
    _title->setText("App visibility");
    _title->setTextFont(&lv_font_montserrat_20);
    _title->setTextColor(lv_color_hex(0x26206A));

    _status = std::make_unique<Label>(_panel->get());
    _status->setWidth(280);
    _status->setTextFont(&lv_font_montserrat_14);
    _status->setTextColor(lv_color_hex(0x26206A));
    _status->setTextAlign(LV_TEXT_ALIGN_CENTER);

    for (const auto& toggle : TOGGLE_LIST) {
        _toggle_rows_widgets.push_back(std::make_unique<ToggleRow>(_panel->get(), toggle.display_name));
    }

    _reset_button = std::make_unique<Button>(_panel->get());
    _reset_button->setSize(280, 44);
    apply_button_common_style(*_reset_button);
    _reset_button->label().setText("Reset");
    _reset_button->onClick().connect([this]() { _reset_requested = true; });

    _back_button = std::make_unique<Button>(_panel->get());
    _back_button->setSize(280, 44);
    apply_button_common_style(*_back_button);
    _back_button->label().setText("Back");
    _back_button->onClick().connect([this]() { _is_done = true; });
}

void FeatureToggleWorker::refresh_rows()
{
    for (size_t i = 0; i < TOGGLE_COUNT; ++i) {
        _toggle_rows_widgets[i]->set_state(_states[i]);
        _toggle_rows_widgets[i]->set_editable(_loaded);
    }
    _reset_button->label().setText(_loaded ? "Reset" : "Retry");
}

void FeatureToggleWorker::load_preferences()
{
    _loaded = load_states(_states);
    refresh_rows();
    _status->setText(_loaded ? HELP : "Unable to load preferences.\nTap Retry or Back.");
}

bool FeatureToggleWorker::save_preferences(const ToggleStates& desired)
{
    if (save_states(desired)) {
        _states = desired;
        _status->setText(HELP);
        refresh_rows();
        return true;
    }
    // The storage API may have failed after writing. Reload its actual state;
    // if that also fails, disable editing until Retry succeeds.
    _loaded = load_states(_states);
    refresh_rows();
    _status->setText(_loaded ? "Unable to save preferences.\nTry again."
                             : "Unable to load preferences.\nTap Retry or Back.");
    mclog::tagWarn(TAG, "Unable to save menu preferences");
    return false;
}

void FeatureToggleWorker::update()
{
    if (_reset_requested) {
        _reset_requested = false;
        if (!_loaded)
            load_preferences();
        else
            save_preferences(default_states());
        return;
    }
    if (!_loaded) return;

    auto desired = _states;
    for (size_t i = 0; i < TOGGLE_COUNT; ++i) desired[i] = _toggle_rows_widgets[i]->get_state();
    if (desired != _states) save_preferences(desired);
}
