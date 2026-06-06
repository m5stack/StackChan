#include <application.h>

#include <audio_codec.h>
#include <board.h>
#include <display.h>
#include <mcp/server.h>
#include <system_info.h>

void Application::Initialize()
{
    auto& board = Board::GetInstance();
    SetDeviceState(kDeviceStateStarting);

    auto* display = board.GetDisplay();
    display->SetupUI();
    display->SetChatMessage("system", SystemInfo::GetUserAgent().c_str());

    auto* codec = board.GetAudioCodec();
    audio_system_.Initialize(codec);
    audio_system_.Start();

    AudioSystemCallbacks callbacks;
    callbacks.on_send_queue_available = [this]() {
        xEventGroupSetBits(event_group_, MAIN_EVENT_SEND_AUDIO);
    };
    callbacks.on_wake_word_detected = [this](const std::string&) {
        xEventGroupSetBits(event_group_, MAIN_EVENT_WAKE_WORD_DETECTED);
    };
    callbacks.on_vad_change = [this](bool) {
        xEventGroupSetBits(event_group_, MAIN_EVENT_VAD_CHANGE);
    };
    audio_system_.SetCallbacks(callbacks);

    state_machine_.AddStateChangeListener([this](DeviceState old_state, DeviceState new_state) {
        OnStateChanged(old_state, new_state);
        xEventGroupSetBits(event_group_, MAIN_EVENT_STATE_CHANGED);
    });

    esp_timer_start_periodic(clock_timer_handle_, 1000000);

    auto& mcp_server = McpServer::GetInstance();
    mcp_server.AddCommonTools();
    mcp_server.AddUserOnlyTools();

    board.SetNetworkEventCallback([this](NetworkEvent event, const std::string& data) { HandleNetworkEvent(event, data); });

    board.StartNetwork();
    display->UpdateStatusBar(true);
}
