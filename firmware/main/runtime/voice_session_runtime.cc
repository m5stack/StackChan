#include <application.h>

#include <assets/lang_config.h>
#include <board.h>

#include <cJSON.h>
#include <esp_log.h>

#include <algorithm>

#define TAG "Application"


void Application::HandleToggleChatEvent()
{
    const DeviceState state = GetDeviceState();

    if (state == kDeviceStateActivating) {
        SetDeviceState(kDeviceStateIdle);
        return;
    }
    if (state == kDeviceStateWifiConfiguring) {
        audio_system_.EnableAudioTesting(true);
        SetDeviceState(kDeviceStateAudioTesting);
        return;
    }
    if (state == kDeviceStateAudioTesting) {
        audio_system_.EnableAudioTesting(false);
        SetDeviceState(kDeviceStateWifiConfiguring);
        return;
    }

    if (protocol_ == nullptr) {
        ESP_LOGE(TAG, "Protocol not initialized");
        return;
    }

    if (state == kDeviceStateIdle) {
        const ListeningMode mode = GetDefaultListeningMode();
        if (!protocol_->IsAudioChannelOpened()) {
            SetDeviceState(kDeviceStateConnecting);
            Schedule([this, mode]() { ContinueOpenAudioChannel(mode); });
            return;
        }
        SetListeningMode(mode);
    } else if (state == kDeviceStateSpeaking) {
        AbortSpeaking(kAbortReasonNone);
    } else if (state == kDeviceStateListening) {
        protocol_->CloseAudioChannel();
    }
}

void Application::HandleStartListeningEvent()
{
    const DeviceState state = GetDeviceState();

    if (state == kDeviceStateActivating) {
        SetDeviceState(kDeviceStateIdle);
        return;
    }
    if (state == kDeviceStateWifiConfiguring) {
        audio_system_.EnableAudioTesting(true);
        SetDeviceState(kDeviceStateAudioTesting);
        return;
    }

    if (protocol_ == nullptr) {
        ESP_LOGE(TAG, "Protocol not initialized");
        return;
    }

    if (state == kDeviceStateIdle) {
        if (!protocol_->IsAudioChannelOpened()) {
            SetDeviceState(kDeviceStateConnecting);
            Schedule([this]() { ContinueOpenAudioChannel(kListeningModeManualStop); });
            return;
        }
        SetListeningMode(kListeningModeManualStop);
    } else if (state == kDeviceStateSpeaking) {
        AbortSpeaking(kAbortReasonNone);
        SetListeningMode(kListeningModeManualStop);
    }
}

void Application::HandleStopListeningEvent()
{
    const DeviceState state = GetDeviceState();
    const ListenStopReason reason = pending_listen_stop_reason_.exchange(kListenStopManualRequest);

    if (state == kDeviceStateAudioTesting) {
        audio_system_.EnableAudioTesting(false);
        SetDeviceState(kDeviceStateWifiConfiguring);
        return;
    }
    if (state == kDeviceStateListening) {
        if (protocol_ != nullptr) {
            protocol_->SendStopListening();
        }
        if (HasDebugEventSubscribers()) {
            cJSON* fields = cJSON_CreateObject();
            if (fields != nullptr) {
                cJSON_AddStringToObject(fields, "reason",
                                        reason == kListenStopAutoTimeout ? "auto_timeout" : "manual_request");
                PublishDebugEvent("listen_stop_handled", fields);
            }
        }
        SetDeviceState(kDeviceStateIdle);
    }
}

void Application::HandleWakeWordDetectedEvent()
{
    if (protocol_ == nullptr) {
        return;
    }

    const DeviceState state = GetDeviceState();
    const auto wake_word = audio_system_.GetLastWakeWord();
    ESP_LOGI(TAG, "Wake word detected: %s (state: %d)", wake_word.c_str(), static_cast<int>(state));
    if (HasDebugEventSubscribers()) {
        cJSON* fields = cJSON_CreateObject();
        if (fields != nullptr) {
            cJSON_AddStringToObject(fields, "wake_word", wake_word.c_str());
            cJSON_AddStringToObject(fields, "state", DeviceStateMachine::GetStateName(state));
            PublishDebugEvent("wake_word_detected", fields);
        }
    }

    if (state == kDeviceStateIdle) {
        StopRemoteWakeMonitoring();
        audio_system_.EncodeWakeWord();

        if (!protocol_->IsAudioChannelOpened()) {
            SetDeviceState(kDeviceStateConnecting);
            Schedule([this, wake_word]() { ContinueWakeWordInvoke(wake_word); });
            return;
        }
        ContinueWakeWordInvoke(wake_word);
        return;
    }

    if (state == kDeviceStateSpeaking || state == kDeviceStateListening) {
        AbortSpeaking(kAbortReasonWakeWordDetected);
        while (audio_system_.PopPacketFromSendQueue()) {
        }

        if (state == kDeviceStateListening) {
            listening_mode_ = GetDefaultListeningMode();
            protocol_->SendStartListening(listening_mode_);
            if (listening_mode_ == kListeningModeAutoStop) {
                StartAutoListenTimeout();
            }
            audio_system_.ResetDecoder();
            audio_system_.PlaySound(Lang::Sounds::OGG_POPUP);
            audio_system_.EnableWakeWordDetection(true);
        } else {
            play_popup_on_listening_ = true;
            SetListeningMode(GetDefaultListeningMode());
        }
        return;
    }

    if (state == kDeviceStateActivating) {
        SetDeviceState(kDeviceStateIdle);
    }
}

void Application::ContinueOpenAudioChannel(ListeningMode mode)
{
    if (GetDeviceState() != kDeviceStateConnecting) {
        return;
    }
    if (protocol_ == nullptr) {
        ESP_LOGW(TAG, "Audio channel open skipped: protocol reset");
        SetDeviceState(kDeviceStateIdle);
        return;
    }

    auto& board = Board::GetInstance();
    board.SetPowerSaveLevel(PowerSaveLevel::PERFORMANCE);

    if (!protocol_->IsAudioChannelOpened() && !protocol_->OpenAudioChannel()) {
        return;
    }

    SetListeningMode(mode);
}

void Application::ContinueWakeWordInvoke(const std::string& wake_word)
{
    const DeviceState state = GetDeviceState();
    if (state != kDeviceStateConnecting && state != kDeviceStateIdle) {
        return;
    }
    if (protocol_ == nullptr) {
        ESP_LOGW(TAG, "Wake word invoke skipped: protocol reset");
        audio_system_.EnableWakeWordDetection(true);
        SetDeviceState(kDeviceStateIdle);
        return;
    }

    auto& board = Board::GetInstance();
    board.SetPowerSaveLevel(PowerSaveLevel::PERFORMANCE);

    if (!protocol_->IsAudioChannelOpened() && !protocol_->OpenAudioChannel()) {
        audio_system_.EnableWakeWordDetection(true);
        return;
    }

    ESP_LOGI(TAG, "Wake word detected: %s", wake_word.c_str());
#if CONFIG_SEND_WAKE_WORD_DATA && !CONFIG_USE_REMOTE_WAKE_WORD
    while (auto packet = audio_system_.PopWakeWordPacket()) {
        protocol_->SendAudio(std::move(packet));
    }
    protocol_->SendWakeWordDetected(wake_word);
    SetListeningMode(GetDefaultListeningMode());
#else
    play_popup_on_listening_ = true;
    SetListeningMode(GetDefaultListeningMode());
#endif
}

void Application::StartRemoteWakeMonitoring()
{
#if CONFIG_USE_REMOTE_WAKE_WORD
    if (protocol_ == nullptr ||
        remote_wake_state_ == RemoteWakeMonitorState::kConnecting ||
        remote_wake_state_ == RemoteWakeMonitorState::kMonitoring ||
        remote_wake_state_ == RemoteWakeMonitorState::kRetryPending) {
        return;
    }
    remote_wake_state_ = RemoteWakeMonitorState::kDisconnected;
    remote_wake_retry_delay_ticks_ = 0;
    remote_wake_retry_due_tick_ = clock_ticks_;
    Schedule([this]() { ContinueRemoteWakeMonitoring(); });
#endif
}

void Application::StopRemoteWakeMonitoring()
{
#if CONFIG_USE_REMOTE_WAKE_WORD
    if (remote_wake_state_ == RemoteWakeMonitorState::kDisabled) {
        return;
    }
    remote_wake_state_ = RemoteWakeMonitorState::kDisabled;
    remote_wake_retry_delay_ticks_ = 0;
    remote_wake_retry_due_tick_ = 0;
    audio_system_.EnableVoiceProcessing(false);
#endif
}

void Application::ContinueRemoteWakeMonitoring()
{
#if CONFIG_USE_REMOTE_WAKE_WORD
    if (remote_wake_state_ == RemoteWakeMonitorState::kDisabled ||
        remote_wake_state_ == RemoteWakeMonitorState::kConnecting ||
        remote_wake_state_ == RemoteWakeMonitorState::kMonitoring ||
        GetDeviceState() != kDeviceStateIdle) {
        return;
    }
    if (remote_wake_state_ == RemoteWakeMonitorState::kRetryPending && clock_ticks_ < remote_wake_retry_due_tick_) {
        return;
    }
    if (protocol_ == nullptr) {
        remote_wake_state_ = RemoteWakeMonitorState::kDisabled;
        return;
    }

    auto& board = Board::GetInstance();
    board.SetPowerSaveLevel(PowerSaveLevel::PERFORMANCE);
    remote_wake_state_ = RemoteWakeMonitorState::kConnecting;

    if (!protocol_->IsAudioChannelOpened() && !protocol_->OpenAudioChannel()) {
        audio_system_.EnableVoiceProcessing(false);
        board.SetPowerSaveLevel(PowerSaveLevel::LOW_POWER);
        remote_wake_retry_delay_ticks_ =
            remote_wake_retry_delay_ticks_ == 0
                ? kRemoteWakeInitialRetryDelayTicks
                : std::min(remote_wake_retry_delay_ticks_ * 2, kRemoteWakeMaxRetryDelayTicks);
        remote_wake_retry_due_tick_ = clock_ticks_ + remote_wake_retry_delay_ticks_;
        remote_wake_state_ = RemoteWakeMonitorState::kRetryPending;
        return;
    }

    remote_wake_retry_delay_ticks_ = 0;
    remote_wake_retry_due_tick_ = 0;
    remote_wake_state_ = RemoteWakeMonitorState::kMonitoring;
    protocol_->SendStartListening(kListeningModeRemoteWake);
    audio_system_.EnableWakeWordDetection(false);
    audio_system_.EnableVoiceProcessing(true);
#endif
}
