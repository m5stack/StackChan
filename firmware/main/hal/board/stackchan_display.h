/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <display/lvgl_display/lvgl_display.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_timer.h>
#include <memory>

class StackChanAvatarDisplay : public LvglDisplay {
private:
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_       = nullptr;
    int speaking_modifier_id_           = -1;
    int idle_motion_modifier_id_        = -1;
    int idle_expression_modifier_id_    = -1;
    int blink_modifier_id_              = -1;
    bool is_sleeping_                   = false;

    lv_obj_t* preview_image_                         = nullptr;
    esp_timer_handle_t preview_timer_                = nullptr;
    std::unique_ptr<LvglImage> preview_image_cached_ = nullptr;

protected:
    virtual bool Lock(int timeout_ms = 0) override;
    virtual void Unlock() override;

public:
    StackChanAvatarDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel, int width, int height,
                           int offset_x, int offset_y, bool mirror_x, bool mirror_y, bool swap_xy);
    virtual ~StackChanAvatarDisplay();

    void InitializeLcdThemes();

    // Override Display methods to control Robot
    virtual void SetEmotion(const char* emotion) override;
    virtual void SetChatMessage(const char* role, const char* content) override;
    virtual void SetPreviewImage(std::unique_ptr<LvglImage> image) override;
    virtual void SetTheme(Theme* theme) override;
    virtual void SetStatus(const char* status) override;

    void SetupXiaoZhiUI();
    void LvglLock();
    void LvglUnlock();
    lv_disp_t* GetLvglDisplay();
};
