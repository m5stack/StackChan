#pragma once

#include <atomic>

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/task.h>

class TaskWorker {
public:
    TaskWorker();
    virtual ~TaskWorker();

    bool StartTask(const char* name, uint32_t stack_words, UBaseType_t priority, BaseType_t core = tskNO_AFFINITY);
    void RequestStop();
    bool WaitStopped(TickType_t timeout);
    bool IsRunning() const;

protected:
    bool StopRequested() const;

private:
    static constexpr EventBits_t kStoppedBit = BIT0;

    static void TaskMain(void* arg);
    virtual void Run() = 0;

    EventGroupHandle_t lifecycle_events_ = nullptr;
    TaskHandle_t task_ = nullptr;
    std::atomic_bool stop_requested_ = false;
    std::atomic_bool running_ = false;
};
