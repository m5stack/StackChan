#include "display.h"

#include <esp_log.h>

#include "settings.h"

namespace {

constexpr char kTag[] = "Display";
constexpr char kSettingsNamespace[] = "display";
constexpr char kThemeKey[] = "theme";

}  // namespace

Display::Display() = default;

Display::~Display() = default;

void Display::SetStatus(const char* status)
{
    ESP_LOGI(kTag, "Status: %s", status != nullptr ? status : "(null)");
}

void Display::ShowNotification(const std::string& notification, int duration_ms)
{
    ShowNotification(notification.c_str(), duration_ms);
}

void Display::ShowNotification(const char* notification, int duration_ms)
{
    ESP_LOGI(kTag, "Notification (%d ms): %s", duration_ms, notification != nullptr ? notification : "(null)");
}

void Display::UpdateStatusBar(bool update_all)
{
    ESP_LOGD(kTag, "UpdateStatusBar(update_all=%d)", update_all);
}

void Display::SetEmotion(const char* emotion)
{
    ESP_LOGI(kTag, "Emotion: %s", emotion != nullptr ? emotion : "(null)");
}

void Display::SetChatMessage(const char* role, const char* content)
{
    ESP_LOGI(kTag, "Chat[%s]: %s", role != nullptr ? role : "unknown", content != nullptr ? content : "(null)");
}

void Display::ClearChatMessages()
{
}

void Display::SetTheme(Theme* theme)
{
    current_theme_ = theme;
    PersistThemeSelection(theme);
}

void Display::SetPowerSaveMode(bool on)
{
    ESP_LOGI(kTag, "Power save mode: %s", on ? "on" : "off");
}

void Display::SetHideSubtitle(bool hide)
{
    ESP_LOGD(kTag, "SetHideSubtitle(%d) ignored by this display", hide);
}

void Display::PersistThemeSelection(const Theme* theme)
{
    if (theme == nullptr) {
        return;
    }
    Settings settings(kSettingsNamespace, true);
    settings.SetString(kThemeKey, theme->name());
}
