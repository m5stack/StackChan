#include <application.h>

#include <board.h>
#include <display.h>
#include <system_info.h>

void Application::HandleSendAudioEvent()
{
    while (auto packet = audio_system_.PopPacketFromSendQueue()) {
        if (protocol_ == nullptr || !protocol_->SendAudio(std::move(packet))) {
            break;
        }
    }
}

void Application::HandleVadChangedEvent()
{
    if (GetDeviceState() != kDeviceStateListening) {
        return;
    }

    auto* led = Board::GetInstance().GetLed();
    if (led != nullptr) {
        led->OnStateChanged();
    }
}

void Application::RunScheduledTasks()
{
    std::unique_lock<std::mutex> lock(mutex_);
    auto tasks = std::move(main_tasks_);
    main_tasks_.clear();
    lock.unlock();

    for (auto& task : tasks) {
        task();
    }
}

void Application::HandleClockTickEvent()
{
    clock_ticks_++;
    Board::GetInstance().GetDisplay()->UpdateStatusBar();
#if CONFIG_USE_REMOTE_WAKE_WORD
    if (remote_wake_state_ == RemoteWakeMonitorState::kRetryPending &&
        GetDeviceState() == kDeviceStateIdle && clock_ticks_ >= remote_wake_retry_due_tick_) {
        Schedule([this]() { ContinueRemoteWakeMonitoring(); });
    }
#endif
    if (clock_ticks_ % 10 == 0) {
        SystemInfo::PrintHeapStats();
    }
}
