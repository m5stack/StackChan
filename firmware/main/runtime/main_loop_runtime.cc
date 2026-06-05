#include <application.h>

#include <assets/lang_config.h>

void Application::Run()
{
    TaskPriorityReset priority(10);

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
            HandleSendAudioEvent();
        }
        if (bits & MAIN_EVENT_WAKE_WORD_DETECTED) {
            HandleWakeWordDetectedEvent();
        }
        if (bits & MAIN_EVENT_VAD_CHANGE) {
            HandleVadChangedEvent();
        }
        if (bits & MAIN_EVENT_SCHEDULE) {
            RunScheduledTasks();
        }
        if (bits & MAIN_EVENT_CLOCK_TICK) {
            HandleClockTickEvent();
        }
    }
}
