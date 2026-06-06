#include <application.h>

#include <assets.h>
#include <assets/lang_config.h>
#include <audio_codec.h>
#include <board.h>
#include <display.h>
#include <settings.h>
#include <websocket_protocol.h>

#include <algorithm>
#include <esp_log.h>

#define TAG "Application"

void Application::ActivationTask()
{
    LoadLocalAssets();
    InitializeProtocol();
    xEventGroupSetBits(event_group_, MAIN_EVENT_ACTIVATION_DONE);
}

void Application::LoadLocalAssets()
{
    if (local_assets_loaded_) {
        return;
    }
    local_assets_loaded_ = true;

    auto* display = Board::GetInstance().GetDisplay();
    auto& assets = Assets::GetInstance();
    if (!assets.partition_valid()) {
        ESP_LOGW(TAG, "Assets partition is disabled for board %s", BOARD_NAME);
        return;
    }

    assets.Apply();
    display->SetChatMessage("system", "");
    display->SetEmotion("microchip_ai");
}

void Application::CheckNewVersion()
{
}

void Application::InitializeProtocol()
{
    if (protocol_ != nullptr) {
        return;
    }

    auto& board = Board::GetInstance();
    auto* display = board.GetDisplay();
    auto* codec = board.GetAudioCodec();

    display->SetStatus(Lang::Strings::LOADING_PROTOCOL);

    Settings mqtt_settings("mqtt", true);
    if (!mqtt_settings.GetString("endpoint").empty()) {
        ESP_LOGW(TAG, "Removing legacy MQTT voice settings");
        mqtt_settings.EraseAll();
    }

    Settings websocket_settings("websocket", true);
    auto websocket_url = websocket_settings.GetString("url");
    if (websocket_url.find("api.tenclass.net") != std::string::npos) {
        ESP_LOGW(TAG, "Removing upstream websocket voice settings");
        websocket_settings.EraseAll();
        websocket_url.clear();
    }

    protocol_ = std::make_unique<WebsocketProtocol>();
    protocol_->OnConnected([this]() { DismissAlert(); });
    protocol_->OnNetworkError([this](const std::string& message) {
        if (remote_wake_state_ == RemoteWakeMonitorState::kConnecting && GetDeviceState() == kDeviceStateIdle) {
            Schedule([this]() {
                if (remote_wake_state_ != RemoteWakeMonitorState::kConnecting) {
                    return;
                }
                audio_system_.EnableVoiceProcessing(false);
                Board::GetInstance().SetPowerSaveLevel(PowerSaveLevel::LOW_POWER);
                remote_wake_retry_delay_ticks_ = remote_wake_retry_delay_ticks_ == 0
                    ? kRemoteWakeInitialRetryDelayTicks
                    : std::min(remote_wake_retry_delay_ticks_ * 2, kRemoteWakeMaxRetryDelayTicks);
                remote_wake_retry_due_tick_ = clock_ticks_ + remote_wake_retry_delay_ticks_;
                remote_wake_state_ = RemoteWakeMonitorState::kRetryPending;
            });
            return;
        }
        last_error_message_ = message;
        xEventGroupSetBits(event_group_, MAIN_EVENT_ERROR);
    });
    protocol_->OnIncomingAudio([this](std::unique_ptr<AudioStreamPacket> packet) {
        if (GetDeviceState() == kDeviceStateSpeaking) {
            audio_system_.PushPacketToDecodeQueue(std::move(packet));
        }
    });
    protocol_->OnAudioChannelOpened([this, codec, &board]() {
        board.SetPowerSaveLevel(PowerSaveLevel::PERFORMANCE);
        if (protocol_->server_sample_rate() != codec->output_sample_rate()) {
            ESP_LOGW(TAG, "Server sample rate %d does not match device output sample rate %d",
                     protocol_->server_sample_rate(), codec->output_sample_rate());
        }
    });
    protocol_->OnAudioChannelClosed([this, &board]() {
        board.SetPowerSaveLevel(PowerSaveLevel::LOW_POWER);
        Schedule([this]() {
            StopRemoteWakeMonitoring();
            auto* current_display = Board::GetInstance().GetDisplay();
            current_display->SetChatMessage("system", "");
            const bool was_idle = GetDeviceState() == kDeviceStateIdle;
            SetDeviceState(kDeviceStateIdle);
            if (was_idle) {
                StartRemoteWakeMonitoring();
            }
        });
    });
    protocol_->OnIncomingJson([this, display](const cJSON* root) { HandleIncomingProtocolJson(root, display); });

    protocol_->Start();
}
