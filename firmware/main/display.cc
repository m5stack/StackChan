#include "display.h"

#include <esp_log.h>

#include "settings.h"

namespace {

constexpr char kTag[] = "Display";

}  // namespace

Display::Display() = default;

Display::~Display() = default;

void Display::SetStatus(const char* status)
{
    ESP_LOGW(kTag, "SetStatus: %s", status);
}

void Display::ShowNotification(const std::string& notification, int duration_ms)
{
    ShowNotification(notification.c_str(), duration_ms);
}

void Display::ShowNotification(const char* notification, int duration_ms)
{
    (void)duration_ms;
    ESP_LOGW(kTag, "ShowNotification: %s", notification);
}

void Display::UpdateStatusBar(bool update_all)
{
    (void)update_all;
}

void Display::SetEmotion(const char* emotion)
{
    ESP_LOGW(kTag, "SetEmotion: %s", emotion);
}

void Display::SetChatMessage(const char* role, const char* content)
{
    ESP_LOGW(kTag, "Role:%s", role);
    ESP_LOGW(kTag, "     %s", content);
}

void Display::ClearChatMessages()
{
}

void Display::SetTheme(Theme* theme)
{
    current_theme_ = theme;
    if (theme == nullptr) {
        return;
    }

    Settings settings("display", true);
    settings.SetString("theme", theme->name());
}

void Display::SetPowerSaveMode(bool on)
{
    ESP_LOGW(kTag, "SetPowerSaveMode: %d", on);
}
