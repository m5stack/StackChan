#include <application.h>

#include <assets.h>
#include <assets/lang_config.h>
#include <audio_codec.h>
#include <board.h>
#include <display.h>
#include <mcp/server.h>
#include <ota.h>
#include <settings.h>
#include <system_info.h>
#include <websocket_protocol.h>

#include <cJSON.h>
#include <driver/gpio.h>
#include <esp_log.h>

#define TAG "Application"

Application::Application()
{
    event_group_ = xEventGroupCreate();

#if CONFIG_USE_DEVICE_AEC && CONFIG_USE_SERVER_AEC
#error "CONFIG_USE_DEVICE_AEC and CONFIG_USE_SERVER_AEC cannot be enabled at the same time"
#elif CONFIG_USE_DEVICE_AEC
    aec_mode_ = kAecOnDeviceSide;
#elif CONFIG_USE_SERVER_AEC
    aec_mode_ = kAecOnServerSide;
#else
    aec_mode_ = kAecOff;
#endif

    esp_timer_create_args_t clock_timer_args = {
        .callback = [](void* arg) {
            auto* app = static_cast<Application*>(arg);
            xEventGroupSetBits(app->event_group_, MAIN_EVENT_CLOCK_TICK);
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "clock_timer",
        .skip_unhandled_events = true,
    };
    esp_timer_create(&clock_timer_args, &clock_timer_handle_);

    esp_timer_create_args_t auto_listen_timeout_timer_args = {
        .callback = [](void* arg) {
            auto* app = static_cast<Application*>(arg);
            app->StopListening();
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "auto_listen_timeout",
        .skip_unhandled_events = true,
    };
    esp_timer_create(&auto_listen_timeout_timer_args, &auto_listen_timeout_timer_handle_);
}

Application::~Application()
{
    if (auto_listen_timeout_timer_handle_ != nullptr) {
        esp_timer_stop(auto_listen_timeout_timer_handle_);
        esp_timer_delete(auto_listen_timeout_timer_handle_);
    }
    if (clock_timer_handle_ != nullptr) {
        esp_timer_stop(clock_timer_handle_);
        esp_timer_delete(clock_timer_handle_);
    }
    if (event_group_ != nullptr) {
        vEventGroupDelete(event_group_);
    }
}

bool Application::SetDeviceState(DeviceState state)
{
    return state_machine_.TransitionTo(state);
}

void Application::Initialize()
{
    auto& board = Board::GetInstance();
    SetDeviceState(kDeviceStateStarting);

    auto* display = board.GetDisplay();
    display->SetupUI();
    display->SetChatMessage("system", SystemInfo::GetUserAgent().c_str());

    auto* codec = board.GetAudioCodec();
    audio_service_.Initialize(codec);
    audio_service_.Start();

    AudioServiceCallbacks callbacks;
    callbacks.on_send_queue_available = [this]() {
        xEventGroupSetBits(event_group_, MAIN_EVENT_SEND_AUDIO);
    };
    callbacks.on_wake_word_detected = [this](const std::string&) {
        xEventGroupSetBits(event_group_, MAIN_EVENT_WAKE_WORD_DETECTED);
    };
    callbacks.on_vad_change = [this](bool) {
        xEventGroupSetBits(event_group_, MAIN_EVENT_VAD_CHANGE);
    };
    audio_service_.SetCallbacks(callbacks);

    state_machine_.AddStateChangeListener([this](DeviceState, DeviceState) {
        xEventGroupSetBits(event_group_, MAIN_EVENT_STATE_CHANGED);
    });

    esp_timer_start_periodic(clock_timer_handle_, 1000000);

    auto& mcp_server = McpServer::GetInstance();
    mcp_server.AddCommonTools();
    mcp_server.AddUserOnlyTools();

    board.SetNetworkEventCallback([this](NetworkEvent event, const std::string& data) {
        auto* display = Board::GetInstance().GetDisplay();

        switch (event) {
            case NetworkEvent::Scanning:
                display->ShowNotification(Lang::Strings::SCANNING_WIFI, 30000);
                xEventGroupSetBits(event_group_, MAIN_EVENT_NETWORK_DISCONNECTED);
                break;
            case NetworkEvent::Connecting: {
                if (data.empty()) {
                    display->SetStatus(Lang::Strings::REGISTERING_NETWORK);
                } else {
                    std::string msg = Lang::Strings::CONNECT_TO;
                    msg += data;
                    msg += "...";
                    display->ShowNotification(msg.c_str(), 30000);
                }
                break;
            }
            case NetworkEvent::Connected: {
                std::string msg = Lang::Strings::CONNECTED_TO;
                msg += data;
                display->ShowNotification(msg.c_str(), 30000);
                xEventGroupSetBits(event_group_, MAIN_EVENT_NETWORK_CONNECTED);
                break;
            }
            case NetworkEvent::Disconnected:
                xEventGroupSetBits(event_group_, MAIN_EVENT_NETWORK_DISCONNECTED);
                break;
            case NetworkEvent::WifiConfigModeEnter:
            case NetworkEvent::WifiConfigModeExit:
                break;
            case NetworkEvent::ModemDetecting:
                display->SetStatus(Lang::Strings::DETECTING_MODULE);
                break;
            case NetworkEvent::ModemErrorNoSim:
                Alert(Lang::Strings::ERROR, Lang::Strings::PIN_ERROR, "triangle_exclamation", Lang::Sounds::OGG_ERR_PIN);
                break;
            case NetworkEvent::ModemErrorRegDenied:
                Alert(Lang::Strings::ERROR, Lang::Strings::REG_ERROR, "triangle_exclamation", Lang::Sounds::OGG_ERR_REG);
                break;
            case NetworkEvent::ModemErrorInitFailed:
                Alert(Lang::Strings::ERROR, Lang::Strings::MODEM_INIT_ERROR, "triangle_exclamation",
                      Lang::Sounds::OGG_EXCLAMATION);
                break;
            case NetworkEvent::ModemErrorTimeout:
                display->SetStatus(Lang::Strings::REGISTERING_NETWORK);
                break;
        }
    });

    board.StartNetwork();
    display->UpdateStatusBar(true);
}

void Application::Run()
{
    vTaskPrioritySet(nullptr, 10);

    constexpr EventBits_t kAllEvents = MAIN_EVENT_SCHEDULE | MAIN_EVENT_SEND_AUDIO | MAIN_EVENT_WAKE_WORD_DETECTED |
                                       MAIN_EVENT_VAD_CHANGE | MAIN_EVENT_CLOCK_TICK | MAIN_EVENT_ERROR |
                                       MAIN_EVENT_NETWORK_CONNECTED | MAIN_EVENT_NETWORK_DISCONNECTED |
                                       MAIN_EVENT_TOGGLE_CHAT | MAIN_EVENT_START_LISTENING |
                                       MAIN_EVENT_STOP_LISTENING | MAIN_EVENT_ACTIVATION_DONE |
                                       MAIN_EVENT_STATE_CHANGED;

    while (true) {
        const EventBits_t bits = xEventGroupWaitBits(event_group_, kAllEvents, pdTRUE, pdFALSE, portMAX_DELAY);

        if (bits & MAIN_EVENT_ERROR) {
            SetDeviceState(kDeviceStateIdle);
            Alert(Lang::Strings::ERROR, last_error_message_.c_str(), "circle_xmark", Lang::Sounds::OGG_EXCLAMATION);
        }
        if (bits & MAIN_EVENT_NETWORK_CONNECTED) {
            HandleNetworkConnectedEvent();
        }
        if (bits & MAIN_EVENT_NETWORK_DISCONNECTED) {
            HandleNetworkDisconnectedEvent();
        }
        if (bits & MAIN_EVENT_ACTIVATION_DONE) {
            HandleActivationDoneEvent();
        }
        if (bits & MAIN_EVENT_STATE_CHANGED) {
            HandleStateChangedEvent();
        }
        if (bits & MAIN_EVENT_TOGGLE_CHAT) {
            HandleToggleChatEvent();
        }
        if (bits & MAIN_EVENT_START_LISTENING) {
            HandleStartListeningEvent();
        }
        if (bits & MAIN_EVENT_STOP_LISTENING) {
            HandleStopListeningEvent();
        }
        if (bits & MAIN_EVENT_SEND_AUDIO) {
            while (auto packet = audio_service_.PopPacketFromSendQueue()) {
                if (protocol_ == nullptr || !protocol_->SendAudio(std::move(packet))) {
                    break;
                }
            }
        }
        if (bits & MAIN_EVENT_WAKE_WORD_DETECTED) {
            HandleWakeWordDetectedEvent();
        }
        if (bits & MAIN_EVENT_VAD_CHANGE) {
            if (GetDeviceState() == kDeviceStateListening) {
                auto* led = Board::GetInstance().GetLed();
                if (led != nullptr) {
                    led->OnStateChanged();
                }
            }
        }
        if (bits & MAIN_EVENT_SCHEDULE) {
            std::unique_lock<std::mutex> lock(mutex_);
            auto tasks = std::move(main_tasks_);
            main_tasks_.clear();
            lock.unlock();
            for (auto& task : tasks) {
                task();
            }
        }
        if (bits & MAIN_EVENT_CLOCK_TICK) {
            clock_ticks_++;
            Board::GetInstance().GetDisplay()->UpdateStatusBar();
            if (clock_ticks_ % 10 == 0) {
                SystemInfo::PrintHeapStats();
            }
        }
    }
}

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
            audio_service_.EnableVoiceProcessing(false);
            audio_service_.EnableWakeWordDetection(true);
            break;
        case kDeviceStateConnecting:
            display->SetStatus(Lang::Strings::CONNECTING);
            display->SetEmotion("neutral");
            display->SetChatMessage("system", "");
            break;
        case kDeviceStateListening:
            display->SetStatus(Lang::Strings::LISTENING);
            display->SetEmotion("neutral");
            if (listening_mode_ == kListeningModeAutoStop) {
                StartAutoListenTimeout();
            } else {
                StopAutoListenTimeout();
            }

            if (play_popup_on_listening_ || !audio_service_.IsAudioProcessorRunning()) {
                if (listening_mode_ == kListeningModeAutoStop) {
                    audio_service_.WaitForPlaybackQueueEmpty();
                }
                if (protocol_ != nullptr) {
                    protocol_->SendStartListening(listening_mode_);
                }
                audio_service_.EnableVoiceProcessing(true);
            }

#ifdef CONFIG_WAKE_WORD_DETECTION_IN_LISTENING
            audio_service_.EnableWakeWordDetection(audio_service_.IsAfeWakeWord());
#else
            audio_service_.EnableWakeWordDetection(false);
#endif

            if (play_popup_on_listening_) {
                play_popup_on_listening_ = false;
                audio_service_.PlaySound(Lang::Sounds::OGG_POPUP);
            }
            break;
        case kDeviceStateSpeaking:
            StopAutoListenTimeout();
            display->SetStatus(Lang::Strings::SPEAKING);
            if (listening_mode_ != kListeningModeRealtime) {
                audio_service_.EnableVoiceProcessing(false);
                audio_service_.EnableWakeWordDetection(audio_service_.IsAfeWakeWord());
            }
            audio_service_.ResetDecoder();
            break;
        case kDeviceStateWifiConfiguring:
            StopAutoListenTimeout();
            audio_service_.EnableVoiceProcessing(false);
            audio_service_.EnableWakeWordDetection(false);
            break;
        default:
            StopAutoListenTimeout();
            break;
    }
}

void Application::HandleToggleChatEvent()
{
    const DeviceState state = GetDeviceState();

    if (state == kDeviceStateActivating) {
        SetDeviceState(kDeviceStateIdle);
        return;
    }
    if (state == kDeviceStateWifiConfiguring) {
        audio_service_.EnableAudioTesting(true);
        SetDeviceState(kDeviceStateAudioTesting);
        return;
    }
    if (state == kDeviceStateAudioTesting) {
        audio_service_.EnableAudioTesting(false);
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
        audio_service_.EnableAudioTesting(true);
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

    if (state == kDeviceStateAudioTesting) {
        audio_service_.EnableAudioTesting(false);
        SetDeviceState(kDeviceStateWifiConfiguring);
        return;
    }
    if (state == kDeviceStateListening) {
        if (protocol_ != nullptr) {
            protocol_->SendStopListening();
        }
        SetDeviceState(kDeviceStateIdle);
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
                app->activation_task_handle_ = nullptr;
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
    SystemInfo::PrintHeapStats();
    SetDeviceState(kDeviceStateIdle);
    Board::GetInstance().GetDisplay()->SetChatMessage("system", "");
    Board::GetInstance().SetPowerSaveLevel(PowerSaveLevel::LOW_POWER);

    Schedule([this]() { audio_service_.PlaySound(Lang::Sounds::OGG_SUCCESS); });
}

void Application::HandleWakeWordDetectedEvent()
{
    if (protocol_ == nullptr) {
        return;
    }

    const DeviceState state = GetDeviceState();
    const auto wake_word = audio_service_.GetLastWakeWord();
    ESP_LOGI(TAG, "Wake word detected: %s (state: %d)", wake_word.c_str(), static_cast<int>(state));

    if (state == kDeviceStateIdle) {
        audio_service_.EncodeWakeWord();

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
        while (audio_service_.PopPacketFromSendQueue()) {
        }

        if (state == kDeviceStateListening) {
            listening_mode_ = GetDefaultListeningMode();
            protocol_->SendStartListening(listening_mode_);
            if (listening_mode_ == kListeningModeAutoStop) {
                StartAutoListenTimeout();
            }
            audio_service_.ResetDecoder();
            audio_service_.PlaySound(Lang::Sounds::OGG_POPUP);
            audio_service_.EnableWakeWordDetection(true);
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

    auto& board = Board::GetInstance();
    board.SetPowerSaveLevel(PowerSaveLevel::PERFORMANCE);

    if (!protocol_->IsAudioChannelOpened() && !protocol_->OpenAudioChannel()) {
        audio_service_.EnableWakeWordDetection(true);
        return;
    }

    ESP_LOGI(TAG, "Wake word detected: %s", wake_word.c_str());
#if CONFIG_SEND_WAKE_WORD_DATA
    while (auto packet = audio_service_.PopWakeWordPacket()) {
        protocol_->SendAudio(std::move(packet));
    }
    protocol_->SendWakeWordDetected(wake_word);
    SetListeningMode(GetDefaultListeningMode());
#else
    play_popup_on_listening_ = true;
    SetListeningMode(GetDefaultListeningMode());
#endif
}

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
        last_error_message_ = message;
        xEventGroupSetBits(event_group_, MAIN_EVENT_ERROR);
    });
    protocol_->OnIncomingAudio([this](std::unique_ptr<AudioStreamPacket> packet) {
        if (GetDeviceState() == kDeviceStateSpeaking) {
            audio_service_.PushPacketToDecodeQueue(std::move(packet));
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
            auto* display = Board::GetInstance().GetDisplay();
            display->SetChatMessage("system", "");
            SetDeviceState(kDeviceStateIdle);
        });
    });
    protocol_->OnIncomingJson([this, display](const cJSON* root) {
        const cJSON* type = cJSON_GetObjectItem(root, "type");
        if (!cJSON_IsString(type)) {
            ESP_LOGW(TAG, "Unknown message without type");
            return;
        }

        if (std::strcmp(type->valuestring, "tts") == 0) {
            const cJSON* state = cJSON_GetObjectItem(root, "state");
            if (cJSON_IsString(state) && std::strcmp(state->valuestring, "start") == 0) {
                Schedule([this]() {
                    aborted_ = false;
                    SetDeviceState(kDeviceStateSpeaking);
                });
            } else if (cJSON_IsString(state) && std::strcmp(state->valuestring, "stop") == 0) {
                Schedule([this]() {
                    if (GetDeviceState() == kDeviceStateSpeaking) {
                        if (listening_mode_ == kListeningModeManualStop) {
                            SetDeviceState(kDeviceStateIdle);
                        } else {
                            SetDeviceState(kDeviceStateListening);
                        }
                    }
                });
            } else if (cJSON_IsString(state) && std::strcmp(state->valuestring, "sentence_start") == 0) {
                const cJSON* text = cJSON_GetObjectItem(root, "text");
                if (cJSON_IsString(text)) {
                    ESP_LOGI(TAG, "<< %s", text->valuestring);
                    Schedule([display, message = std::string(text->valuestring)]() {
                        display->SetChatMessage("assistant", message.c_str());
                    });
                }
            }
            return;
        }

        if (std::strcmp(type->valuestring, "stt") == 0) {
            const cJSON* text = cJSON_GetObjectItem(root, "text");
            if (cJSON_IsString(text)) {
                ESP_LOGI(TAG, ">> %s", text->valuestring);
                Schedule([display, message = std::string(text->valuestring)]() {
                    display->SetChatMessage("user", message.c_str());
                });
            }
            return;
        }

        if (std::strcmp(type->valuestring, "llm") == 0) {
            const cJSON* emotion = cJSON_GetObjectItem(root, "emotion");
            if (cJSON_IsString(emotion)) {
                Schedule([display, emotion_str = std::string(emotion->valuestring)]() {
                    display->SetEmotion(emotion_str.c_str());
                });
            }
            return;
        }

        if (std::strcmp(type->valuestring, "mcp") == 0) {
            const cJSON* payload = cJSON_GetObjectItem(root, "payload");
            if (cJSON_IsObject(payload)) {
                McpServer::GetInstance().ParseMessage(payload);
            }
            return;
        }

        if (std::strcmp(type->valuestring, "system") == 0) {
            const cJSON* command = cJSON_GetObjectItem(root, "command");
            if (cJSON_IsString(command) && std::strcmp(command->valuestring, "reboot") == 0) {
                Schedule([this]() { Reboot(); });
            }
            return;
        }

        if (std::strcmp(type->valuestring, "alert") == 0) {
            const cJSON* status = cJSON_GetObjectItem(root, "status");
            const cJSON* message = cJSON_GetObjectItem(root, "message");
            const cJSON* emotion = cJSON_GetObjectItem(root, "emotion");
            if (cJSON_IsString(status) && cJSON_IsString(message) && cJSON_IsString(emotion)) {
                Alert(status->valuestring, message->valuestring, emotion->valuestring, Lang::Sounds::OGG_VIBRATION);
            }
            return;
        }

#if CONFIG_RECEIVE_CUSTOM_MESSAGE
        if (std::strcmp(type->valuestring, "custom") == 0) {
            const cJSON* payload = cJSON_GetObjectItem(root, "payload");
            if (cJSON_IsObject(payload)) {
                char* printed = cJSON_PrintUnformatted(payload);
                std::string payload_str = printed != nullptr ? printed : "{}";
                if (printed != nullptr) {
                    cJSON_free(printed);
                }
                Schedule([display, payload_str]() { display->SetChatMessage("system", payload_str.c_str()); });
            }
            return;
        }
#endif

        ESP_LOGW(TAG, "Unknown message type: %s", type->valuestring);
    });

    protocol_->Start();
}

void Application::StartAutoListenTimeout()
{
    if (auto_listen_timeout_timer_handle_ == nullptr) {
        return;
    }
    esp_timer_stop(auto_listen_timeout_timer_handle_);
    esp_timer_start_once(auto_listen_timeout_timer_handle_, 12 * 1000 * 1000);
}

void Application::StopAutoListenTimeout()
{
    if (auto_listen_timeout_timer_handle_ != nullptr) {
        esp_timer_stop(auto_listen_timeout_timer_handle_);
    }
}

void Application::ShowActivationCode(const std::string& code, const std::string& message)
{
    (void)code;
    Board::GetInstance().GetDisplay()->SetChatMessage("system", message.c_str());
}

void Application::Schedule(std::function<void()>&& callback)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        main_tasks_.push_back(std::move(callback));
    }
    xEventGroupSetBits(event_group_, MAIN_EVENT_SCHEDULE);
}

void Application::Alert(const char* status, const char* message, const char* emotion, const std::string_view& sound)
{
    ESP_LOGW(TAG, "Alert [%s] %s: %s", emotion, status, message);
    auto* display = Board::GetInstance().GetDisplay();
    display->SetStatus(status);
    display->SetEmotion(emotion);
    display->SetChatMessage("system", message);
    if (!sound.empty()) {
        audio_service_.PlaySound(sound);
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
    xEventGroupSetBits(event_group_, MAIN_EVENT_START_LISTENING);
}

void Application::StopListening()
{
    xEventGroupSetBits(event_group_, MAIN_EVENT_STOP_LISTENING);
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

void Application::Reboot()
{
    ESP_LOGI(TAG, "Rebooting...");
    if (protocol_ != nullptr && protocol_->IsAudioChannelOpened()) {
        protocol_->CloseAudioChannel();
    }
    protocol_.reset();
    audio_service_.Stop();
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
        audio_service_.EncodeWakeWord();
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
    audio_service_.Stop();
    vTaskDelay(pdMS_TO_TICKS(1000));

    const bool upgrade_success = Ota::Upgrade(url, [this, display](int progress, size_t speed) {
        char buffer[32] = {};
        std::snprintf(buffer, sizeof(buffer), "%d%% %uKB/s", progress, static_cast<unsigned>(speed / 1024));
        Schedule([display, message = std::string(buffer)]() { display->SetChatMessage("system", message.c_str()); });
    });

    if (!upgrade_success) {
        ESP_LOGE(TAG, "Firmware upgrade failed");
        audio_service_.Start();
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
    return audio_service_.IsIdle();
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

void Application::SetAecMode(AecMode mode)
{
    aec_mode_ = mode;
    Schedule([this]() {
        auto* display = Board::GetInstance().GetDisplay();
        switch (aec_mode_) {
            case kAecOff:
                audio_service_.EnableDeviceAec(false);
                display->ShowNotification(Lang::Strings::RTC_MODE_OFF);
                break;
            case kAecOnServerSide:
                audio_service_.EnableDeviceAec(false);
                display->ShowNotification(Lang::Strings::RTC_MODE_ON);
                break;
            case kAecOnDeviceSide:
                audio_service_.EnableDeviceAec(true);
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
    audio_service_.PlaySound(sound);
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
