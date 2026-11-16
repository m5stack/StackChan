/* SPDX-License-Identifier: MIT */
#include "workers.h"

using namespace setup_workers;
using namespace uitk::lvgl_cpp;

namespace {
std::string description(const char* name, const network_diagnostics::ServiceResult& result)
{
    std::string text = std::string(name) + "\n" + result.detail;
    if (result.http_status > 0)
        text += "\nHTTP " + std::to_string(result.http_status) + " | " + std::to_string(result.elapsed_ms) + " ms";
    return text;
}

void updateResult(Label& label, std::string& previous, const char* name,
                  const network_diagnostics::ServiceResult& result)
{
    const auto text = description(name, result);
    if (text == previous) return;
    previous = text;
    label.setText(text);
    using network_diagnostics::Status;
    const auto color = result.status == Status::Available   ? 0x217A43
                       : result.status == Status::Reachable ? 0x916400
                       : result.status == Status::Failed    ? 0xB3261E
                                                            : 0x26206A;
    label.setTextColor(lv_color_hex(color));
}
}  // namespace

NetworkTestWorker::NetworkTestWorker()
{
    _panel = std::make_unique<Container>(lv_screen_active());
    _panel->setSize(320, 240);
    _panel->setBgColor(lv_color_hex(0xEDF4FF));
    _panel->setBorderWidth(0);
    _panel->setRadius(0);
    _panel->setScrollDir(LV_DIR_VER);
    _panel->setScrollbarMode(LV_SCROLLBAR_MODE_ACTIVE);
    lv_obj_set_style_pad_top(_panel->get(), 38, 0);
    lv_obj_set_style_pad_bottom(_panel->get(), 32, 0);
    lv_obj_set_style_pad_left(_panel->get(), 16, 0);
    lv_obj_set_style_pad_right(_panel->get(), 16, 0);
    lv_obj_set_style_pad_row(_panel->get(), 18, 0);
    lv_obj_set_flex_flow(_panel->get(), LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(_panel->get(), LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    auto createLabel = [&]() {
        auto label = std::make_unique<Label>(_panel->get());
        label->setWidth(280);
        label->setTextFont(&lv_font_montserrat_16);
        label->setTextColor(lv_color_hex(0x26206A));
        label->setLongMode(LV_LABEL_LONG_MODE_WRAP);
        return label;
    };
    _title = createLabel();
    _title->setText("Testing service");
    _title->setTextFont(&lv_font_montserrat_24);
    _wifi    = createLabel();
    _xiaozhi = createLabel();
    _m5stack = createLabel();
    _retry   = std::make_unique<Button>(_panel->get());
    apply_button_common_style(*_retry);
    _retry->setSize(280, 44);
    _retry->label().setText("Test Again");
    _retry->onClick().connect([this]() { _retry_clicked = true; });
    _back = std::make_unique<Button>(_panel->get());
    apply_button_common_style(*_back);
    _back->setSize(280, 44);
    _back->label().setText("Back");
    _back->onClick().connect([this]() { _back_clicked = true; });
    _diagnostics.start();
    update();
}

NetworkTestWorker::~NetworkTestWorker() = default;

void NetworkTestWorker::update()
{
    if (_back_clicked) {
        _is_done = true;
        return;
    }
    if (_retry_clicked) {
        _retry_clicked = false;
        _diagnostics.start();
    }
    const auto results = _diagnostics.snapshot();
    if (results.wifi != _last_wifi) {
        _last_wifi = results.wifi;
        _wifi->setText(results.wifi);
    }
    updateResult(*_xiaozhi, _last_xiaozhi, "Xiaozhi", results.xiaozhi);
    updateResult(*_m5stack, _last_m5stack, "M5Stack", results.m5stack);
    if (results.running)
        _retry->addState(LV_STATE_DISABLED);
    else
        _retry->removeState(LV_STATE_DISABLED);
}
