#include <application.h>

#include <assets/lang_config.h>
#include <board.h>
#include <display.h>
#include <system_info.h>

#include <cJSON.h>
#include <esp_log.h>

#define TAG "Application"

void Application::HandleStateChangedEvent()
{
    const DeviceState new_state = state_machine_.GetState();
    clock_ticks_ = 0;

    auto& board = Board::GetInstance();
    auto* display = board.GetDisplay();
    if (auto* led = board.GetLed(); led != nullptr) {
        led->OnStateChanged();
    }

    switch (new_state) {
        case kDeviceStateUnknown:
        case kDeviceStateIdle:
            StopAutoListenTimeout();
            display->SetStatus(Lang::Strings::STANDBY);
            display->ClearChatMessages();
            display->SetEmotion("neutral");
            audio_system_.EnableVoiceProcessing(false);
            audio_system_.EnableWakeWordDetection(true);
            break;
        case kDeviceStateConnecting:
            display->SetStatus(Lang::Strings::CONNECTING);
            display->SetEmotion("neutral");
            display->SetChatMessage("system", "");
            break;
        case kDeviceStateListening:
            display->SetStatus(Lang::Strings::LISTENING);
            display->SetEmotion("neutral");
            if (HasDebugEventSubscribers()) {
                cJSON* fields = cJSON_CreateObject();
                if (fields != nullptr) {
                    const char* mode = "manual";
                    if (listening_mode_ == kListeningModeAutoStop) {
                        mode = "auto";
                    } else if (listening_mode_ == kListeningModeRealtime) {
                        mode = "realtime";
                    }
                    cJSON_AddStringToObject(fields, "mode", mode);
                    PublishDebugEvent("listen_enter", fields);
                }
            }
            if (listening_mode_ == kListeningModeAutoStop) {
                StartAutoListenTimeout();
            } else {
                StopAutoListenTimeout();
            }

            if (play_popup_on_listening_ || !audio_system_.IsAudioProcessorRunning()) {
                if (listening_mode_ == kListeningModeAutoStop) {
                    audio_system_.WaitForPlaybackQueueEmpty();
                }
                if (protocol_ != nullptr) {
                    protocol_->SendStartListening(listening_mode_);
                }
                audio_system_.EnableVoiceProcessing(true);
            }

#ifdef CONFIG_WAKE_WORD_DETECTION_IN_LISTENING
            audio_system_.EnableWakeWordDetection(audio_system_.SupportsConcurrentVoiceSessionWakeDetection());
#else
            audio_system_.EnableWakeWordDetection(false);
#endif

            if (play_popup_on_listening_) {
                play_popup_on_listening_ = false;
                audio_system_.PlaySound(Lang::Sounds::OGG_POPUP);
            }
            break;
        case kDeviceStateSpeaking:
            StopAutoListenTimeout();
            display->SetStatus(Lang::Strings::SPEAKING);
            if (listening_mode_ != kListeningModeRealtime) {
                audio_system_.EnableVoiceProcessing(false);
                audio_system_.EnableWakeWordDetection(audio_system_.SupportsConcurrentVoiceSessionWakeDetection());
            }
            audio_system_.ResetDecoder();
            break;
        case kDeviceStateWifiConfiguring:
            StopAutoListenTimeout();
            audio_system_.EnableVoiceProcessing(false);
            audio_system_.EnableWakeWordDetection(false);
            break;
        default:
            StopAutoListenTimeout();
            break;
    }
}

void Application::HandleNetworkConnectedEvent()
{
    ESP_LOGI(TAG, "Network connected");
    const DeviceState state = GetDeviceState();

    if (state == kDeviceStateStarting || state == kDeviceStateWifiConfiguring) {
        SetDeviceState(kDeviceStateActivating);
        if (activation_task_handle_ != nullptr) {
            ESP_LOGW(TAG, "Activation task already running");
        } else {
            xTaskCreate([](void* arg) {
                auto* app = static_cast<Application*>(arg);
                app->ActivationTask();
                vTaskDelete(nullptr);
            }, "activation", 4096 * 2, this, 2, &activation_task_handle_);
        }
    }

    Board::GetInstance().GetDisplay()->UpdateStatusBar(true);
}

void Application::HandleNetworkDisconnectedEvent()
{
    const DeviceState state = GetDeviceState();
    if (protocol_ != nullptr &&
        (state == kDeviceStateConnecting || state == kDeviceStateListening || state == kDeviceStateSpeaking)) {
        ESP_LOGI(TAG, "Closing audio channel due to network disconnection");
        protocol_->CloseAudioChannel();
    }

    Board::GetInstance().GetDisplay()->UpdateStatusBar(true);
}

void Application::HandleActivationDoneEvent()
{
    ESP_LOGI(TAG, "Activation done");
    activation_task_handle_ = nullptr;
    SystemInfo::PrintHeapStats();
    SetDeviceState(kDeviceStateIdle);
    Board::GetInstance().GetDisplay()->SetChatMessage("system", "");
    Board::GetInstance().SetPowerSaveLevel(PowerSaveLevel::LOW_POWER);

    Schedule([this]() { audio_system_.PlaySound(Lang::Sounds::OGG_SUCCESS); });
}

void Application::OnStateChanged(DeviceState old_state, DeviceState new_state)
{
    if (!HasDebugEventSubscribers()) {
        return;
    }

    cJSON* fields = cJSON_CreateObject();
    if (fields != nullptr) {
        cJSON_AddStringToObject(fields, "old_state", DeviceStateMachine::GetStateName(old_state));
        cJSON_AddStringToObject(fields, "new_state", DeviceStateMachine::GetStateName(new_state));
        PublishDebugEvent("state_transition", fields);
    }
}
