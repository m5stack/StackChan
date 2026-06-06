#include "task_worker.h"

#include <cstdlib>

#include <esp_log.h>

namespace {

constexpr char kTag[] = "TaskWorker";

}  // namespace

TaskWorker::TaskWorker()
{
    lifecycle_events_ = xEventGroupCreate();
}

TaskWorker::~TaskWorker()
{
    RequestStop();
    if (!WaitStopped(pdMS_TO_TICKS(1000))) {
        ESP_LOGE(kTag, "Worker failed to stop before destruction");
        std::abort();
    }
    if (lifecycle_events_ != nullptr) {
        vEventGroupDelete(lifecycle_events_);
    }
}

bool TaskWorker::StartTask(const char* name, uint32_t stack_words, UBaseType_t priority, BaseType_t core)
{
    if (running_) {
        return true;
    }
    if (lifecycle_events_ == nullptr) {
        return false;
    }

    stop_requested_ = false;
    xEventGroupClearBits(lifecycle_events_, kStoppedBit);

    BaseType_t result = pdFAIL;
    if (core == tskNO_AFFINITY) {
        result = xTaskCreate(&TaskWorker::TaskMain, name, stack_words, this, priority, &task_);
    } else {
        result =
            xTaskCreatePinnedToCore(&TaskWorker::TaskMain, name, stack_words, this, priority, &task_, core);
    }
    if (result != pdPASS) {
        task_ = nullptr;
        xEventGroupSetBits(lifecycle_events_, kStoppedBit);
        return false;
    }

    running_ = true;
    return true;
}

void TaskWorker::RequestStop()
{
    stop_requested_ = true;
}

bool TaskWorker::WaitStopped(TickType_t timeout)
{
    if (lifecycle_events_ == nullptr) {
        return true;
    }
    const EventBits_t bits = xEventGroupWaitBits(lifecycle_events_, kStoppedBit, pdFALSE, pdTRUE, timeout);
    return (bits & kStoppedBit) != 0;
}

bool TaskWorker::IsRunning() const
{
    return running_;
}

bool TaskWorker::StopRequested() const
{
    return stop_requested_;
}

void TaskWorker::TaskMain(void* arg)
{
    auto* self = static_cast<TaskWorker*>(arg);
    self->Run();
    self->running_ = false;
    self->task_ = nullptr;
    xEventGroupSetBits(self->lifecycle_events_, kStoppedBit);
    vTaskDelete(nullptr);
}
