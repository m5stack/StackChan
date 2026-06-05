#include "press_to_talk_mcp_tool.h"

#include <esp_log.h>

namespace {
constexpr char kTag[] = "PressToTalkMcpTool";
constexpr char kVendorNamespace[] = "vendor";
constexpr char kPressToTalkKey[] = "press_to_talk";
constexpr char kModePressToTalk[] = "press_to_talk";
constexpr char kModeClickToTalk[] = "click_to_talk";
} // namespace

PressToTalkMcpTool::PressToTalkMcpTool()
    : press_to_talk_enabled_(false)
{
}

void PressToTalkMcpTool::Initialize()
{
    Settings settings(kVendorNamespace);
    press_to_talk_enabled_ = settings.GetInt(kPressToTalkKey, 0) != 0;

    auto& mcp_server = McpServer::GetInstance();
    mcp_server.AddTool(
        "self.set_press_to_talk",
        "Switch between press to talk mode and click to talk mode. "
        "The mode can be `press_to_talk` or `click_to_talk`.",
        PropertyList({Property("mode", kPropertyTypeString)}),
        [this](const PropertyList& properties) -> ReturnValue { return HandleSetPressToTalk(properties); });

    ESP_LOGI(kTag, "Initialized press-to-talk mode: %s",
             press_to_talk_enabled_ ? kModePressToTalk : kModeClickToTalk);
}

bool PressToTalkMcpTool::IsPressToTalkEnabled() const
{
    return press_to_talk_enabled_;
}

ReturnValue PressToTalkMcpTool::HandleSetPressToTalk(const PropertyList& properties)
{
    const auto mode = properties["mode"].value<std::string>();

    if (mode == kModePressToTalk) {
        SetPressToTalkEnabled(true);
        ESP_LOGI(kTag, "Switched to %s mode", kModePressToTalk);
        return true;
    }
    if (mode == kModeClickToTalk) {
        SetPressToTalkEnabled(false);
        ESP_LOGI(kTag, "Switched to %s mode", kModeClickToTalk);
        return true;
    }

    throw std::runtime_error("Invalid mode: " + mode);
}

void PressToTalkMcpTool::SetPressToTalkEnabled(bool enabled)
{
    press_to_talk_enabled_ = enabled;

    Settings settings(kVendorNamespace, true);
    settings.SetInt(kPressToTalkKey, enabled ? 1 : 0);
    ESP_LOGI(kTag, "Press to talk enabled: %d", enabled);
}
