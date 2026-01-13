/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "view.h"
#include <mooncake_log.h>
#include <assets/assets.h>
#include <functional>
#include <hal/hal.h>
#include <cstdint>
#include <vector>

using namespace view;
using namespace uitk;
using namespace uitk::lvgl_cpp;

/* -------------------------------------------------------------------------- */
/*                    Dynamic backgroud color handle class                    */
/* -------------------------------------------------------------------------- */
class DynamicBgColor {
public:
    std::function<void(const uint32_t& bgColor)> onBgColorChanged;

    void init(const std::vector<uint32_t>& stepColors, int stepGap)
    {
        _step_colors = stepColors;
        _step_gap    = stepGap;

        _bg_color.duration = 0.3;
        _bg_color.begin();

        jumpTo(0);
    }

    void jumpTo(int index)
    {
        if (index < 0 || index >= _step_colors.size()) {
            return;
        }
        _bg_color.teleport(_step_colors[index]);
        _current_index = index;

        if (onBgColorChanged) {
            onBgColorChanged(_step_colors[index]);
        }
    }

    void update(int scrollValue)
    {
        _last_index = _current_index;

        // Update current index
        _current_index = (scrollValue + _step_gap / 2) / _step_gap;
        if (_current_index < 0) {
            _current_index = 0;
        }
        if (_current_index >= _step_colors.size()) {
            _current_index = _step_colors.size() - 1;
        }

        // If index changed
        if (_last_index != _current_index) {
            // mclog::tagInfo(_tag, "index changed from {} to {}", _last_index, _current_index);
            _bg_color = _step_colors[_current_index];
        }

        // Update background color
        _bg_color.update();
        if (!_bg_color.done()) {
            if (onBgColorChanged) {
                onBgColorChanged(_bg_color.toHex());
            }
        }
    }

private:
    std::vector<uint32_t> _step_colors;
    int _current_index = 0;
    int _last_index    = 0;
    int _step_gap      = 0;
    color::AnimateRgb_t _bg_color;
};

/* -------------------------------------------------------------------------- */
/*                               Page indicator                               */
/* -------------------------------------------------------------------------- */
class PageIndicator {
public:
    const int dot_size     = 8;
    const int dot_size_big = 14;
    const int dot_gap      = 16;

    void init(int pageNum, int pageGap, lv_obj_t* parent, int posX, int posY)
    {
        _page_num = pageNum;
        _page_gap = pageGap;

        _panel = std::make_unique<Container>(parent);
        _panel->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
        _panel->addFlag(LV_OBJ_FLAG_FLOATING);
        _panel->setAlign(LV_ALIGN_CENTER);
        _panel->setPadding(0, 0, 24, 24);
        _panel->setPos(posX, posY);
        _panel->setBorderWidth(0);
        _panel->setHeight(24);
        _panel->setWidth((pageNum * dot_size) + (pageNum - 1) * (dot_gap - dot_size) + 24 * 2);
        _panel->setBgOpa(0);

        for (int i = 0; i < pageNum; i++) {
            _dots.push_back(std::make_unique<Container>(_panel->get()));
            _dots.back()->setAlign(LV_ALIGN_CENTER);
            _dots.back()->setPos(i * dot_gap - (pageNum - 1) * dot_gap / 2, 0);
            _dots.back()->setBgColor(lv_color_hex(0xFFFFFF));
            _dots.back()->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
            _dots.back()->setRadius(LV_RADIUS_CIRCLE);
            _dots.back()->setSize(dot_size, dot_size);
            _dots.back()->setBorderWidth(0);
        }

        jumpTo(0);
    }

    void jumpTo(int index)
    {
        if (index < 0 || index >= _page_num) {
            return;
        }
        _current_index = index;
        _last_index    = index;
        update_dots();
    }

    void update(int scrollValue)
    {
        _last_index = _current_index;

        _current_index = (scrollValue + _page_gap / 2) / _page_gap;
        if (_current_index < 0) {
            _current_index = 0;
        }
        if (_current_index >= _page_num) {
            _current_index = _page_num - 1;
        }

        if (_last_index != _current_index) {
            update_dots();
        }
    }

private:
    int _page_num = 0;
    int _page_gap = 0;

    int _current_index = 0;
    int _last_index    = 0;

    std::unique_ptr<Container> _panel;
    std::vector<std::unique_ptr<Container>> _dots;

    void update_dots()
    {
        for (int i = 0; i < _page_num; i++) {
            if (i == _current_index) {
                _dots[i]->setSize(dot_size_big, dot_size_big);
                _dots[i]->setOpa(255);
            } else {
                _dots[i]->setSize(dot_size, dot_size);
                _dots[i]->setOpa(128);
            }
        }
    }
};

/* -------------------------------------------------------------------------- */
/*                             Dynamic icon label                             */
/* -------------------------------------------------------------------------- */
class DynamicIconLabel {
public:
    const int show_range      = 50;
    const int hidden_pos_y    = -150;
    const int visible_pos_y   = -99;
    const int transition_zone = 30;

    void init(const std::vector<std::string>& iconLabelTexts, int iconGap, lv_obj_t* parent)
    {
        _icon_label_texts = iconLabelTexts;
        _icon_gap         = iconGap;

        // Create floating label
        _label = std::make_unique<Label>(parent);
        _label->setTextColor(lv_color_hex(0x000000));
        _label->setTextFont(&MontserratSemiBold26);
        _label->setAlign(LV_ALIGN_CENTER);
        _label->addFlag(LV_OBJ_FLAG_FLOATING);
        _label->setOpa(233);

        // Setup animation
        _pos_y_anim                                 = std::make_unique<AnimateValue>();
        _pos_y_anim->springOptions().visualDuration = 0.3;
        _pos_y_anim->springOptions().bounce         = 0.1;
        _pos_y_anim->begin();

        jumpTo(0);
    }

    void jumpTo(int index)
    {
        if (index < 0 || index >= _icon_label_texts.size()) {
            return;
        }

        _current_index = index;
        _last_index    = index;
        _is_visible    = true;

        // Update label
        _label->setText(_icon_label_texts[index]);
        _label->setPos(0, visible_pos_y);
        _pos_y_anim->teleport(visible_pos_y);
    }

    void update(int scrollValue)
    {
        _last_index = _current_index;

        // Calculate current icon index and distance to icon center
        _current_index        = (scrollValue + _icon_gap / 2) / _icon_gap;
        int icon_center_pos_x = _current_index * _icon_gap;
        int distance_to_icon  = std::abs(scrollValue - icon_center_pos_x);

        // Clamp index
        if (_current_index < 0) {
            _current_index = 0;
        }
        if (_current_index >= _icon_label_texts.size()) {
            _current_index = _icon_label_texts.size() - 1;
        }

        // Check if label should be visible
        bool should_be_visible = (distance_to_icon <= show_range);

        // If index changed, update label text
        if (_last_index != _current_index) {
            _label->setText(_icon_label_texts[_current_index]);
        }

        // Handle visibility state change
        if (should_be_visible && !_is_visible) {
            // Show label
            _pos_y_anim->move(visible_pos_y);
            _is_visible = true;
        } else if (!should_be_visible && _is_visible) {
            // Hide label
            _pos_y_anim->move(hidden_pos_y);
            _is_visible = false;
        }

        // Update animation and apply position
        _pos_y_anim->update();
        _label->setY(_pos_y_anim->directValue());

        // Update opacity based on distance when in transition zone
        if (should_be_visible && distance_to_icon > (show_range - transition_zone)) {
            // Fade out as approaching edge
            float fade_ratio = 1.0f - (float)(distance_to_icon - (show_range - transition_zone)) / transition_zone;
            _label->setOpa(233 * fade_ratio);
        } else if (should_be_visible) {
            _label->setOpa(233);
        }
    }

private:
    std::vector<std::string> _icon_label_texts;
    int _icon_gap      = 0;
    int _current_index = 0;
    int _last_index    = 0;
    bool _is_visible   = false;

    std::unique_ptr<Label> _label;
    std::unique_ptr<AnimateValue> _pos_y_anim;
};

static std::string _tag        = "LauncherView";
static constexpr int _icon_gap = 320;

static int _last_clicked_icon_pos_x = -1;
static std::unique_ptr<DynamicBgColor> _dynamic_bg_color;
static std::unique_ptr<PageIndicator> _page_indicator;
static std::unique_ptr<DynamicIconLabel> _dynamic_icon_label;

LauncherView::~LauncherView()
{
    _icon_images.clear();
    _icon_panels.clear();
    _lr_indicators_images.clear();
    _lr_indicator_panels.clear();
    _panel.reset();
    _dynamic_bg_color.reset();
    _page_indicator.reset();
    _dynamic_icon_label.reset();
}

void LauncherView::init(std::vector<mooncake::AppProps_t> appPorps)
{
    mclog::tagInfo(_tag, "init");

    /* ------------------------------ Screen setup ------------------------------ */
    ScreenActive screen;
    screen.removeFlag(LV_OBJ_FLAG_SCROLLABLE);

    /* ---------------------------------- Panel --------------------------------- */
    _panel = std::make_unique<Container>(lv_screen_active());
    _panel->setAlign(LV_ALIGN_CENTER);
    _panel->setSize(320, 240);
    _panel->setRadius(0);
    _panel->setBorderWidth(0);
    _panel->setScrollbarMode(LV_SCROLLBAR_MODE_OFF);
    _panel->setBgColor(lv_color_hex(0x33CC99));
    _panel->addFlag(LV_OBJ_FLAG_SCROLL_ONE);
    lv_obj_set_scroll_snap_x(_panel->get(), LV_SCROLL_SNAP_CENTER);

    /* ---------------------------------- Icons --------------------------------- */
    int icon_x = 0;
    int icon_y = 0;
    std::vector<std::string> icon_label_texts;
    for (const auto& props : appPorps) {
        // mclog::tagInfo(_tag, "name: {}, id: {}", props.info.name, props.appID);

        // Icon panel
        _icon_panels.push_back(std::make_unique<Container>(_panel->get()));
        _icon_panels.back()->setAlign(LV_ALIGN_CENTER);
        _icon_panels.back()->setSize(190, 160);
        _icon_panels.back()->setPos(icon_x, icon_y);
        _icon_panels.back()->setBorderWidth(0);
        _icon_panels.back()->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
        _icon_panels.back()->setBgOpa(0);

        // Icon click callback
        auto app_id = props.appID;
        auto pos_x  = icon_x;
        _icon_panels.back()->onClick().connect([&, app_id, pos_x]() {
            _clicked_app_id          = app_id;
            _last_clicked_icon_pos_x = pos_x;
        });

        // Collect icon label texts
        icon_label_texts.push_back(props.info.name);

        // Icon image
        if (props.info.icon != nullptr) {
            _icon_images.push_back(std::make_unique<Image>(_icon_panels.back()->get()));
            _icon_images.back()->setSrc(props.info.icon);
            _icon_images.back()->setAlign(LV_ALIGN_CENTER);
        }

        icon_x += _icon_gap;
    }

    /* ------------------------------ LR indicators ----------------------------- */
    // Scroll to nearby icon handler with wrap-around
    int total_icons            = appPorps.size();
    auto scroll_to_nearby_icon = [&, total_icons](int direction) {
        auto current_scroll_x = _panel->getScrollX();
        int current_index     = (current_scroll_x + _icon_gap / 2) / _icon_gap;
        int target_index      = current_index + direction;

        // Wrap around at boundaries
        if (target_index < 0) {
            target_index = total_icons - 1;
        } else if (target_index >= total_icons) {
            target_index = 0;
        }

        int target_x        = target_index * _icon_gap;
        int scroll_distance = target_x - current_scroll_x;
        _panel->scrollBy(-scroll_distance, 0, LV_ANIM_ON);
    };

    // Go left indicator
    _lr_indicator_panels.push_back(std::make_unique<Container>(_panel->get()));
    _lr_indicator_panels.back()->setAlign(LV_ALIGN_CENTER);
    _lr_indicator_panels.back()->setSize(52, 160);
    _lr_indicator_panels.back()->setPos(-134, 0);
    _lr_indicator_panels.back()->setBorderWidth(0);
    _lr_indicator_panels.back()->addFlag(LV_OBJ_FLAG_FLOATING);
    _lr_indicator_panels.back()->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
    _lr_indicator_panels.back()->setBgOpa(0);
    _lr_indicator_panels.back()->onClick().connect([scroll_to_nearby_icon]() { scroll_to_nearby_icon(-1); });

    _lr_indicators_images.push_back(std::make_unique<Image>(_lr_indicator_panels.back()->get()));
    _lr_indicators_images.back()->setSrc(&icon_indicator_left);
    _lr_indicators_images.back()->align(LV_ALIGN_CENTER, 0, 0);

    // Go right indicator
    _lr_indicator_panels.push_back(std::make_unique<Container>(_panel->get()));
    _lr_indicator_panels.back()->setAlign(LV_ALIGN_CENTER);
    _lr_indicator_panels.back()->setSize(52, 160);
    _lr_indicator_panels.back()->setPos(134, 0);
    _lr_indicator_panels.back()->setBorderWidth(0);
    _lr_indicator_panels.back()->addFlag(LV_OBJ_FLAG_FLOATING);
    _lr_indicator_panels.back()->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
    _lr_indicator_panels.back()->setBgOpa(0);
    _lr_indicator_panels.back()->onClick().connect([scroll_to_nearby_icon]() { scroll_to_nearby_icon(1); });

    _lr_indicators_images.push_back(std::make_unique<Image>(_lr_indicator_panels.back()->get()));
    _lr_indicators_images.back()->setSrc(&icon_indicator_right);
    _lr_indicators_images.back()->align(LV_ALIGN_CENTER, 0, 0);

    /* ---------------------------- Dynamic bg color ---------------------------- */
    _dynamic_bg_color = std::make_unique<DynamicBgColor>();

    std::vector<uint32_t> step_colors;
    step_colors.resize(appPorps.size());
    for (size_t i = 0; i < appPorps.size(); i++) {
        uint32_t color = 0xDADADA;
        if (appPorps[i].info.userData != nullptr) {
            color = *(uint32_t*)appPorps[i].info.userData;
        }
        step_colors[i] = color;
    }

    _dynamic_bg_color->onBgColorChanged = [&](const uint32_t& bgColor) {
        // mclog::tagInfo(_tag, "bg color changed to {:06X}", bgColor);
        _panel->setBgColor(lv_color_hex(bgColor));
    };

    _dynamic_bg_color->init(step_colors, _icon_gap);

    /* ------------------------------ Page indicator ---------------------------- */
    _page_indicator = std::make_unique<PageIndicator>();
    _page_indicator->init(appPorps.size(), _icon_gap, _panel->get(), 0, 103);

    /* --------------------------- Dynamic icon label --------------------------- */
    _dynamic_icon_label = std::make_unique<DynamicIconLabel>();
    _dynamic_icon_label->init(icon_label_texts, _icon_gap, _panel->get());

    /* ----------------------------- History restore ---------------------------- */
    bool need_restore      = false;
    int restore_icon_pos_x = -1;

    // If warm boot was requested
    if (GetHAL().getWarmRebootTarget() >= 0) {
        auto app_index = GetHAL().getWarmRebootTarget();
        mclog::tagInfo(_tag, "warm boot was requested, app index: {}", app_index);
        app_index          = uitk::clamp(app_index, 0, static_cast<int>(appPorps.size()) - 1);
        restore_icon_pos_x = app_index * _icon_gap;
        need_restore       = true;
        GetHAL().clearWarmRebootRequest();
    }

    if (_last_clicked_icon_pos_x != -1) {
        // mclog::tagInfo(_tag, "navigate to last clicked icon, pos x: {}", _last_clicked_icon_pos_x);
        restore_icon_pos_x       = _last_clicked_icon_pos_x;
        need_restore             = true;
        _last_clicked_icon_pos_x = -1;
    }

    if (need_restore) {
        _panel->scrollBy(-restore_icon_pos_x, 0, LV_ANIM_OFF);

        _dynamic_bg_color->jumpTo(restore_icon_pos_x / _icon_gap);
        _page_indicator->jumpTo(restore_icon_pos_x / _icon_gap);
        _dynamic_icon_label->jumpTo(restore_icon_pos_x / _icon_gap);

        _state = STATE_NORMAL;
    }

    // If first create
    else {
        // Setup startup animation
        // x for pos_y, y for radius
        _startup_anim = std::make_unique<AnimateVector2>();

        _startup_anim->x.springOptions().damping        = 12.0;
        _startup_anim->y.delay                          = 0.15;
        _startup_anim->y.springOptions().visualDuration = 0.4;
        _startup_anim->y.springOptions().bounce         = 0.05;

        _startup_anim->teleport(240, 120);
        _panel->setY(_startup_anim->directValue().x);
        _panel->setRadius(_startup_anim->directValue().y);
        _startup_anim->move(0, 0);

        _state = STATE_STARTUP;
    }

    // Destory boot logo label
    GetHAL().bootLogo.reset();
}

void LauncherView::update()
{
    switch (_state) {
        case STATE_STARTUP:
            handle_state_startup();
            break;
        case STATE_NORMAL:
            handle_state_normal();
            break;
        default:
            break;
    }
}

void LauncherView::handle_state_startup()
{
    _startup_anim->update();

    _panel->setY(_startup_anim->directValue().x);
    _panel->setRadius(_startup_anim->directValue().y);

    if (_startup_anim->done()) {
        _startup_anim.reset();
        _state = STATE_NORMAL;
    }
}

void LauncherView::handle_state_normal()
{
    if (_clicked_app_id != -1) {
        if (onAppClicked) {
            onAppClicked(_clicked_app_id);
        }
        _clicked_app_id = -1;
    }

    int scroll_x = _panel->getScrollX();
    // mclog::tagInfo(_tag, "scroll x: {}", scroll_x);

    _dynamic_bg_color->update(scroll_x);
    _page_indicator->update(scroll_x);
    _dynamic_icon_label->update(scroll_x);
}
