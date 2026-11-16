/*
 * SPDX-License-Identifier: MIT
 */
#include "reefwatch_view.h"
#include <hal/hal.h>
#include <stackchan/stackchan.h>
#include <mooncake_log.h>
#include <sdkconfig.h>
#include <lvgl.h>
#include <map>
#include <memory>
#include <sstream>
#include <string>

#if CONFIG_STACKCHAN_REEFWATCH_ENABLE

static const std::string_view _tag = "reefwatch-view";

// Live frames keep the view open; if they stop arriving, close after this long
static constexpr uint32_t _live_idle_ms = 3000;

static lv_obj_t* _panel     = nullptr;
static lv_obj_t* _image     = nullptr;
static lv_obj_t* _caption   = nullptr;
static lv_timer_t* _timer   = nullptr;
static std::shared_ptr<LvglImage> _frame;  // keeps the pixels alive while LVGL draws them
static std::string _camera;
static bool _detection_on_screen = false;  // blocks live frames until it times out or is tapped
static bool _reacted             = false;  // LEDs changed, put them back on hide
static bool _head_turned         = false;
static std::string _reacted_camera;
static std::map<std::string, int> _camera_yaw;

// "front=-45,garden=45" -> {front: -45, garden: 45}
static void parse_camera_yaw()
{
    std::istringstream list(CONFIG_STACKCHAN_REEFWATCH_CAMERA_YAW);
    std::string item;
    while (std::getline(list, item, ',')) {
        auto eq = item.find('=');
        if (eq != std::string::npos) {
            _camera_yaw[item.substr(0, eq)] = std::atoi(item.c_str() + eq + 1);
        }
    }
}

static void hide()
{
    lv_obj_add_flag(_panel, LV_OBJ_FLAG_HIDDEN);
    lv_timer_pause(_timer);
    lv_image_set_src(_image, nullptr);
    _frame.reset();

    _detection_on_screen = false;
    if (_reacted) {
        _reacted = false;
        GetStackChan().ledMood().clearAlert();  // back to the mood or manual color
    }
    if (_head_turned) {
        _head_turned = false;
        GetStackChan().motion().yawServo().moveWithSpeed(0, 300);
    }
}

static void on_timer(lv_timer_t* timer)
{
    hide();
}

static void on_click(lv_event_t* e)
{
    // Tap a detection to keep watching that camera, tap the live view to stop it
    if (GetHAL().getReefwatchLiveCamera().empty() && !_camera.empty()) {
        GetHAL().setReefwatchLiveCamera(_camera);
        _detection_on_screen = false;
        lv_label_set_text_fmt(_caption, "%s  -  starting live view...", _camera.c_str());
        lv_timer_set_period(_timer, _live_idle_ms + 2000);  // reefwatch polls every 2 s
        lv_timer_reset(_timer);
    } else {
        GetHAL().setReefwatchLiveCamera("");
        hide();
    }
}

static void react_to_detection(const std::string& camera)
{
    // Amber LEDs, within the 0-168 range hal_mcp treats as safe
    GetStackChan().ledMood().setAlert(168, 90, 0);

    auto yaw = _camera_yaw.find(camera);
    if (yaw != _camera_yaw.end()) {
        GetStackChan().motion().yawServo().moveWithSpeed(yaw->second * 10, 300);
        _head_turned = true;
    }
    _reacted        = true;
    _reacted_camera = camera;
}

static void show(const ReefwatchFrame_t& frame)
{
    // A live frame never replaces a detection that is still on screen
    if (frame.isLive && _detection_on_screen) {
        return;
    }

    _frame  = frame.image;
    _camera = frame.camera;
    lv_image_set_src(_image, _frame->image_dsc());

    if (frame.isLive) {
        lv_label_set_text_fmt(_caption, "%s  -  LIVE", frame.camera.c_str());
        lv_timer_set_period(_timer, _live_idle_ms);
    } else {
        lv_label_set_text_fmt(_caption, "%s  -  %s", frame.camera.c_str(), frame.labels.c_str());
        lv_timer_set_period(_timer, CONFIG_STACKCHAN_REEFWATCH_SHOW_MS);
        if (!_reacted || frame.camera != _reacted_camera) {
            react_to_detection(frame.camera);
        }
        _detection_on_screen = true;
    }

    lv_obj_remove_flag(_panel, LV_OBJ_FLAG_HIDDEN);
    lv_timer_reset(_timer);
    lv_timer_resume(_timer);
}

void view::reefwatch_view_init()
{
    parse_camera_yaw();

    LvglLockGuard lock;

    _panel = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(_panel);
    lv_obj_set_size(_panel, 320, 240);
    lv_obj_set_style_bg_color(_panel, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(_panel, LV_OPA_COVER, 0);
    lv_obj_remove_flag(_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(_panel, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(_panel, on_click, LV_EVENT_CLICKED, nullptr);

    _image = lv_image_create(_panel);
    lv_obj_center(_image);

    _caption = lv_label_create(_panel);
    lv_obj_set_width(_caption, 320);
    lv_obj_align(_caption, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(_caption, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(_caption, LV_OPA_60, 0);
    lv_obj_set_style_text_color(_caption, lv_color_white(), 0);
    lv_obj_set_style_pad_all(_caption, 4, 0);
    lv_label_set_long_mode(_caption, LV_LABEL_LONG_DOT);

    _timer = lv_timer_create(on_timer, CONFIG_STACKCHAN_REEFWATCH_SHOW_MS, nullptr);
    lv_timer_pause(_timer);

    GetHAL().onReefwatchFrame.connect([](const ReefwatchFrame_t& frame) {
        LvglLockGuard lock;
        show(frame);
    });

    mclog::tagInfo(_tag, "ready");
}

#else

void view::reefwatch_view_init()
{
}

#endif
