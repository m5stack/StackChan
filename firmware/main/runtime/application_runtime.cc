#include <application.h>

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

void Application::Schedule(std::function<void()>&& callback)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        main_tasks_.push_back(std::move(callback));
    }
    xEventGroupSetBits(event_group_, MAIN_EVENT_SCHEDULE);
}
