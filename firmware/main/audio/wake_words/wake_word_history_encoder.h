#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <vector>

class WakeWordHistoryEncoder {
public:
    WakeWordHistoryEncoder(const char* tag, size_t max_frames, size_t stack_size_bytes);
    ~WakeWordHistoryEncoder();

    void Append(const int16_t* samples, size_t sample_count);
    void Append(const std::vector<int16_t>& samples);
    void Clear();
    void Start();
    bool Pop(std::vector<uint8_t>& packet);

private:
    struct EncodeJob;

    static void TaskEntry(void* arg);
    void RunEncodeJob(EncodeJob& job);
    void Publish(std::vector<uint8_t>&& packet);
    void PublishEnd();

    const char* tag_;
    size_t max_frames_ = 0;
    size_t stack_size_bytes_ = 0;

    TaskHandle_t task_ = nullptr;
    StaticTask_t* task_buffer_ = nullptr;
    StackType_t* task_stack_ = nullptr;

    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<std::vector<int16_t>> pcm_;
    std::deque<std::vector<uint8_t>> opus_;
    bool encoding_ = false;
};
