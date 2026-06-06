#include <application.h>

#include <assets/lang_config.h>
#include <board.h>
#include <display.h>
#include <ota.h>

#include <cJSON.h>
#include <esp_log.h>

#define TAG "Application"

void Application::ShowActivationCode(const std::string& code, const std::string& message)
{
    (void)code;
    Board::GetInstance().GetDisplay()->SetChatMessage("system", message.c_str());
}

void Application::Alert(const char* status, const char* message, const char* emotion, const std::string_view& sound)
{
    ESP_LOGW(TAG, "Alert [%s] %s: %s", emotion, status, message);
    auto* display = Board::GetInstance().GetDisplay();
    display->SetStatus(status);
    display->SetEmotion(emotion);
    display->SetChatMessage("system", message);
    if (!sound.empty()) {
        audio_system_.PlaySound(sound);
    }
}

void Application::DismissAlert()
{
    if (GetDeviceState() == kDeviceStateIdle) {
        auto* display = Board::GetInstance().GetDisplay();
        display->SetStatus(Lang::Strings::STANDBY);
        display->SetEmotion("neutral");
        display->SetChatMessage("system", "");
    }
}

void Application::AbortSpeaking(AbortReason reason)
{
    ESP_LOGI(TAG, "Abort speaking");
    aborted_ = true;
    if (protocol_ != nullptr) {
        protocol_->SendAbortSpeaking(reason);
    }
}

void Application::ToggleChatState()
{
    xEventGroupSetBits(event_group_, MAIN_EVENT_TOGGLE_CHAT);
}

void Application::StartListening()
{
    if (HasDebugEventSubscribers()) {
        cJSON* fields = cJSON_CreateObject();
        if (fields != nullptr) {
            cJSON_AddStringToObject(fields, "mode", "manual");
            PublishDebugEvent("listen_start_requested", fields);
        }
    }
    xEventGroupSetBits(event_group_, MAIN_EVENT_START_LISTENING);
}

void Application::StopListening()
{
    RequestStopListening(kListenStopManualRequest);
}

void Application::SetListeningMode(ListeningMode mode)
{
    listening_mode_ = mode;
    SetDeviceState(kDeviceStateListening);
}

ListeningMode Application::GetDefaultListeningMode() const
{
    return aec_mode_ == kAecOff ? kListeningModeAutoStop : kListeningModeRealtime;
}

void Application::RequestStopListening(ListenStopReason reason)
{
    pending_listen_stop_reason_.store(reason);
    if (HasDebugEventSubscribers()) {
        cJSON* fields = cJSON_CreateObject();
        if (fields != nullptr) {
            cJSON_AddStringToObject(fields, "reason",
                                    reason == kListenStopAutoTimeout ? "auto_timeout" : "manual_request");
            PublishDebugEvent("listen_stop_requested", fields);
        }
    }
    xEventGroupSetBits(event_group_, MAIN_EVENT_STOP_LISTENING);
}

void Application::Reboot()
{
    ESP_LOGI(TAG, "Rebooting...");
    if (protocol_ != nullptr && protocol_->IsAudioChannelOpened()) {
        protocol_->CloseAudioChannel();
    }
    protocol_.reset();
    audio_system_.Stop();
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
}

void Application::WakeWordInvoke(const std::string& wake_word)
{
    if (protocol_ == nullptr) {
        return;
    }

    const DeviceState state = GetDeviceState();
    if (state == kDeviceStateIdle) {
        audio_system_.EncodeWakeWord();
        if (!protocol_->IsAudioChannelOpened()) {
            SetDeviceState(kDeviceStateConnecting);
            Schedule([this, wake_word]() { ContinueWakeWordInvoke(wake_word); });
            return;
        }
        ContinueWakeWordInvoke(wake_word);
    } else if (state == kDeviceStateSpeaking) {
        Schedule([this]() { AbortSpeaking(kAbortReasonNone); });
    } else if (state == kDeviceStateListening) {
        Schedule([this]() {
            if (protocol_ != nullptr) {
                protocol_->CloseAudioChannel();
            }
        });
    }
}

bool Application::UpgradeFirmware(const std::string& url, const std::string& version)
{
    auto& board = Board::GetInstance();
    auto* display = board.GetDisplay();
    const std::string version_info = version.empty() ? "(Manual upgrade)" : version;

    if (protocol_ != nullptr && protocol_->IsAudioChannelOpened()) {
        protocol_->CloseAudioChannel();
    }

    Alert(Lang::Strings::OTA_UPGRADE, Lang::Strings::UPGRADING, "download", Lang::Sounds::OGG_UPGRADE);
    vTaskDelay(pdMS_TO_TICKS(3000));

    SetDeviceState(kDeviceStateUpgrading);
    display->SetChatMessage("system", (std::string(Lang::Strings::NEW_VERSION) + version_info).c_str());
    board.SetPowerSaveLevel(PowerSaveLevel::PERFORMANCE);
    audio_system_.Stop();
    vTaskDelay(pdMS_TO_TICKS(1000));

    const bool upgrade_success = Ota::Upgrade(url, [this, display](int progress, size_t speed) {
        char buffer[32] = {};
        std::snprintf(buffer, sizeof(buffer), "%d%% %uKB/s", progress, static_cast<unsigned>(speed / 1024));
        Schedule([display, message = std::string(buffer)]() { display->SetChatMessage("system", message.c_str()); });
    });

    if (!upgrade_success) {
        ESP_LOGE(TAG, "Firmware upgrade failed");
        audio_system_.Start();
        board.SetPowerSaveLevel(PowerSaveLevel::LOW_POWER);
        Alert(Lang::Strings::ERROR, Lang::Strings::UPGRADE_FAILED, "circle_xmark", Lang::Sounds::OGG_EXCLAMATION);
        vTaskDelay(pdMS_TO_TICKS(3000));
        return false;
    }

    display->SetChatMessage("system", "Upgrade successful, rebooting...");
    vTaskDelay(pdMS_TO_TICKS(1000));
    Reboot();
    return true;
}

bool Application::CanEnterSleepMode()
{
    if (GetDeviceState() != kDeviceStateIdle) {
        return false;
    }
    if (protocol_ != nullptr && protocol_->IsAudioChannelOpened()) {
        return false;
    }
    return audio_system_.IsIdle();
}

void Application::SendMcpMessage(const std::string& payload)
{
    Schedule([this, payload]() {
        if (protocol_ != nullptr) {
            protocol_->SendMcpMessage(payload);
        }
        if (mcp_broadcast_callback_) {
            mcp_broadcast_callback_(payload);
        }
    });
}

void Application::RegisterMcpBroadcastCallback(std::function<void(const std::string&)> callback)
{
    mcp_broadcast_callback_ = std::move(callback);
}

void Application::RegisterDebugEventCallback(std::function<bool()> has_subscribers,
                                             std::function<void(const char*, cJSON*)> callback)
{
    has_debug_event_subscribers_callback_ = std::move(has_subscribers);
    debug_event_callback_                 = std::move(callback);
}

void Application::SetAecMode(AecMode mode)
{
    aec_mode_ = mode;
    Schedule([this]() {
        auto* display = Board::GetInstance().GetDisplay();
        switch (aec_mode_) {
            case kAecOff:
                audio_system_.EnableDeviceAec(false);
                display->ShowNotification(Lang::Strings::RTC_MODE_OFF);
                break;
            case kAecOnServerSide:
                audio_system_.EnableDeviceAec(false);
                display->ShowNotification(Lang::Strings::RTC_MODE_ON);
                break;
            case kAecOnDeviceSide:
                audio_system_.EnableDeviceAec(true);
                display->ShowNotification(Lang::Strings::RTC_MODE_ON);
                break;
        }

        if (protocol_ != nullptr && protocol_->IsAudioChannelOpened()) {
            protocol_->CloseAudioChannel();
        }
    });
}

void Application::PlaySound(const std::string_view& sound)
{
    audio_system_.PlaySound(sound);
}

void Application::ResetProtocol()
{
    Schedule([this]() {
        if (protocol_ != nullptr && protocol_->IsAudioChannelOpened()) {
            protocol_->CloseAudioChannel();
        }
        protocol_.reset();
        InitializeProtocol();
    });
}

bool Application::HasDebugEventSubscribers() const
{
    return has_debug_event_subscribers_callback_ != nullptr && has_debug_event_subscribers_callback_();
}

void Application::PublishDebugEvent(const char* type, cJSON* fields)
{
    if (debug_event_callback_ == nullptr || !HasDebugEventSubscribers()) {
        if (fields != nullptr) {
            cJSON_Delete(fields);
        }
        return;
    }
    debug_event_callback_(type, fields);
}
