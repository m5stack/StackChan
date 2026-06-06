#pragma once

#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/task.h>

#include <atomic>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

#include "audio_system.h"
#include "device_state.h"
#include "device_state_machine.h"
#include "ota.h"
#include "protocol.h"

enum class NetworkEvent;
class Display;
struct cJSON;

#define MAIN_EVENT_SCHEDULE             (1 << 0)
#define MAIN_EVENT_SEND_AUDIO           (1 << 1)
#define MAIN_EVENT_WAKE_WORD_DETECTED   (1 << 2)
#define MAIN_EVENT_VAD_CHANGE           (1 << 3)
#define MAIN_EVENT_ERROR                (1 << 4)
#define MAIN_EVENT_ACTIVATION_DONE      (1 << 5)
#define MAIN_EVENT_CLOCK_TICK           (1 << 6)
#define MAIN_EVENT_NETWORK_CONNECTED    (1 << 7)
#define MAIN_EVENT_NETWORK_DISCONNECTED (1 << 8)
#define MAIN_EVENT_TOGGLE_CHAT          (1 << 9)
#define MAIN_EVENT_START_LISTENING      (1 << 10)
#define MAIN_EVENT_STOP_LISTENING       (1 << 11)
#define MAIN_EVENT_STATE_CHANGED        (1 << 12)

enum AecMode {
    kAecOff,
    kAecOnDeviceSide,
    kAecOnServerSide,
};

enum ListenStopReason {
    kListenStopManualRequest,
    kListenStopAutoTimeout,
};

enum class RemoteWakeMonitorState {
    kDisabled,
    kDisconnected,
    kConnecting,
    kMonitoring,
    kRetryPending,
};

static constexpr uint32_t kRemoteWakeInitialRetryDelayTicks = 5;
static constexpr uint32_t kRemoteWakeMaxRetryDelayTicks = 30;

class Application {
public:
    static Application& GetInstance()
    {
        static Application instance;
        return instance;
    }

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    void Initialize();
    void Run();

    DeviceState GetDeviceState() const { return state_machine_.GetState(); }
    bool IsVoiceDetected() const { return audio_system_.IsVoiceDetected(); }
    bool SetDeviceState(DeviceState state);
    void Schedule(std::function<void()>&& callback);
    void Alert(const char* status, const char* message, const char* emotion = "", const std::string_view& sound = "");
    void DismissAlert();
    void AbortSpeaking(AbortReason reason);
    void ToggleChatState();
    void StartListening();
    void StopListening();
    void Reboot();
    void WakeWordInvoke(const std::string& wake_word);
    bool UpgradeFirmware(const std::string& url, const std::string& version = "");
    bool CanEnterSleepMode();
    void SendMcpMessage(const std::string& payload);
    void RegisterMcpBroadcastCallback(std::function<void(const std::string&)> callback);
    void RegisterDebugEventCallback(std::function<bool()> has_subscribers,
                                    std::function<void(const char*, cJSON*)> callback);
    void SetAecMode(AecMode mode);
    AecMode GetAecMode() const { return aec_mode_; }
    void PlaySound(const std::string_view& sound);
    AudioSystem& GetAudioSystem() { return audio_system_; }
    void ResetProtocol();

private:
    Application();
    ~Application();

    std::mutex mutex_;
    std::deque<std::function<void()>> main_tasks_;
    std::unique_ptr<Protocol> protocol_;
    EventGroupHandle_t event_group_ = nullptr;
    esp_timer_handle_t clock_timer_handle_ = nullptr;
    esp_timer_handle_t auto_listen_timeout_timer_handle_ = nullptr;
    DeviceStateMachine state_machine_;
    ListeningMode listening_mode_ = kListeningModeAutoStop;
    AecMode aec_mode_ = kAecOff;
    std::string last_error_message_;
    AudioSystem audio_system_;
    std::unique_ptr<Ota> ota_;

    std::function<void(const std::string&)> mcp_broadcast_callback_;
    std::function<bool()> has_debug_event_subscribers_callback_;
    std::function<void(const char*, cJSON*)> debug_event_callback_;
    std::atomic<ListenStopReason> pending_listen_stop_reason_{kListenStopManualRequest};

    bool has_server_time_ = false;
    bool aborted_ = false;
    bool local_assets_loaded_ = false;
    bool play_popup_on_listening_ = false;
    RemoteWakeMonitorState remote_wake_state_ = RemoteWakeMonitorState::kDisabled;
    uint32_t remote_wake_retry_delay_ticks_ = 0;
    uint32_t remote_wake_retry_due_tick_ = 0;
    uint32_t clock_ticks_ = 0;
    TaskHandle_t activation_task_handle_ = nullptr;

    void HandleStateChangedEvent();
    void HandleToggleChatEvent();
    void HandleStartListeningEvent();
    void HandleStopListeningEvent();
    void HandleNetworkConnectedEvent();
    void HandleNetworkDisconnectedEvent();
    void HandleActivationDoneEvent();
    void HandleWakeWordDetectedEvent();
    void HandleNetworkEvent(NetworkEvent event, const std::string& data);
    void HandleSendAudioEvent();
    void HandleVadChangedEvent();
    void RunScheduledTasks();
    void HandleClockTickEvent();
    void ContinueOpenAudioChannel(ListeningMode mode);
    void ContinueWakeWordInvoke(const std::string& wake_word);
    void StartRemoteWakeMonitoring();
    void StopRemoteWakeMonitoring();
    void ContinueRemoteWakeMonitoring();
    void ActivationTask();
    void LoadLocalAssets();
    void CheckNewVersion();
    void InitializeProtocol();
    void HandleIncomingProtocolJson(const cJSON* root, Display* display);
    bool HandleIncomingTtsStart(const cJSON* root, Display* display);
    bool HandleIncomingTtsSentence(const cJSON* root, Display* display);
    bool HandleIncomingTtsStop(const cJSON* root, Display* display);
    bool HandleIncomingSttTranscript(const cJSON* root, Display* display);
    bool HandleIncomingUiEmotion(const cJSON* root, Display* display);
    bool HandleIncomingUiAlert(const cJSON* root);
    bool HandleIncomingSystemReboot(const cJSON* root);
    bool HandleIncomingMcp(const cJSON* root);
    bool HandleIncomingListenDetect(const cJSON* root);
    bool HandleIncomingUiCustom(const cJSON* root, Display* display);
    void StartAutoListenTimeout();
    void StopAutoListenTimeout();
    void ShowActivationCode(const std::string& code, const std::string& message);
    void SetListeningMode(ListeningMode mode);
    ListeningMode GetDefaultListeningMode() const;
    void OnStateChanged(DeviceState old_state, DeviceState new_state);
    void RequestStopListening(ListenStopReason reason);
    bool HasDebugEventSubscribers() const;
    void PublishDebugEvent(const char* type, cJSON* fields);
};

class TaskPriorityReset {
public:
    explicit TaskPriorityReset(BaseType_t priority)
    {
        original_priority_ = uxTaskPriorityGet(nullptr);
        vTaskPrioritySet(nullptr, priority);
    }

    ~TaskPriorityReset()
    {
        vTaskPrioritySet(nullptr, original_priority_);
    }

private:
    BaseType_t original_priority_;
};
