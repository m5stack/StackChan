#pragma once

#include <chrono>
#include <string>
#include <utility>

#include <esp_log.h>
#include <esp_pm.h>
#include <esp_timer.h>

#include "emoji_collection.h"
#include <lvgl.h>

class Theme {
public:
    explicit Theme(std::string name) : name_(std::move(name)) {}
    virtual ~Theme() = default;

    const std::string& name() const { return name_; }

private:
    std::string name_;
};

class Display {
public:
    Display();
    virtual ~Display();

    virtual void SetStatus(const char* status);
    virtual void ShowNotification(const char* notification, int duration_ms = 3000);
    virtual void ShowNotification(const std::string& notification, int duration_ms = 3000);
    virtual void SetEmotion(const char* emotion);
    virtual void SetChatMessage(const char* role, const char* content);
    virtual void ClearChatMessages();
    virtual void SetTheme(Theme* theme);
    virtual Theme* GetTheme() { return current_theme_; }
    virtual void UpdateStatusBar(bool update_all = false);
    virtual void SetPowerSaveMode(bool on);
    virtual void SetHideSubtitle(bool hide);
    virtual void SetupUI()
    {
        setup_ui_called_ = true;
    }

    int width() const { return width_; }
    int height() const { return height_; }
    bool IsSetupUICalled() const { return setup_ui_called_; }

protected:
    void PersistThemeSelection(const Theme* theme);

    int width_ = 0;
    int height_ = 0;
    bool setup_ui_called_ = false;
    Theme* current_theme_ = nullptr;

    friend class DisplayLockGuard;
    virtual bool Lock(int timeout_ms = 0) = 0;
    virtual void Unlock() = 0;
};

class DisplayLockGuard {
public:
    explicit DisplayLockGuard(Display* display) : display_(display)
    {
        if (!display_->Lock(30000)) {
            ESP_LOGE("Display", "Failed to lock display");
        }
    }

    ~DisplayLockGuard()
    {
        display_->Unlock();
    }

private:
    Display* display_;
};

class NoDisplay : public Display {
private:
    bool Lock(int timeout_ms = 0) override
    {
        (void)timeout_ms;
        return true;
    }

    void Unlock() override {}
};
