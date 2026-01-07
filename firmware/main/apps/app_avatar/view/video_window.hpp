/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <lvgl.h>
#include <hal/hal.h>
#include <smooth_lvgl.hpp>
#include <uitk/short_namespace.hpp>
#include <memory>

namespace view {

class VideoWindow {
public:
    VideoWindow(lv_obj_t* parent)
    {
        _image = std::make_unique<uitk::lvgl_cpp::Image>(parent);
        _image->align(LV_ALIGN_CENTER, 0, 0);
        _image->setSize(320, 240);
        _image->setHidden(true);

        _on_ws_video_mode_change_id = GetHAL().onWsVideoModeChange.connect([this](bool enabled) {
            LvglLockGuard lock;
            if (enabled) {
                _image->setHidden(false);
            } else {
                _image->setHidden(true);
            }
        });

        _on_ws_video_frame_id = GetHAL().onWsVideoFrame.connect([this](std::shared_ptr<LvglImage> img) {
            LvglLockGuard lock;
            _image_cached = img;
            auto img_dsc  = _image_cached->image_dsc();
            _image->setSrc(img_dsc);
        });
    }

    ~VideoWindow()
    {
        GetHAL().onWsVideoModeChange.disconnect(_on_ws_video_mode_change_id);
        GetHAL().onWsVideoFrame.disconnect(_on_ws_video_frame_id);
    }

private:
    std::unique_ptr<uitk::lvgl_cpp::Image> _image;
    std::shared_ptr<LvglImage> _image_cached;

    int _on_ws_video_mode_change_id = -1;
    int _on_ws_video_frame_id       = -1;
};

}  // namespace view
