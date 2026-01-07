/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <string>
#include <chrono>
#include <vector>
#include <memory>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

#include "stackchan/utils/object_pool.h"
#include "hal/utils/simple_audio_player/simple_audio_player.h"

class ReminderItem : public stackchan::Poolable {
public:
    ReminderItem(int duration_s, const std::string& msg);

    bool IsDue() const;
    bool IsTriggered() const
    {
        return triggered_;
    }
    void SetTriggered(bool t)
    {
        triggered_ = t;
    }
    const std::string& GetMessage() const
    {
        return message_;
    }

private:
    std::string message_;
    std::chrono::steady_clock::time_point target_time_;
    bool triggered_ = false;
};

class ReminderManager {
public:
    static ReminderManager& GetInstance();

    // 初始化并启动后台线程
    void Start();

    // 创建一个提醒
    // duration_s: 多少秒后提醒
    // message: 提醒内容
    // 返回: 提醒 ID
    int CreateReminder(int duration_s, const std::string& message);

    // 停止/关闭提醒
    // id: 提醒 ID
    void StopReminder(int id);

private:
    ReminderManager();
    ~ReminderManager();

    void WorkerThread();

    SemaphoreHandle_t mutex_ = nullptr;
    stackchan::ObjectPool<ReminderItem> pool_;
    // SimpleAudioPlayer audio_player_;

    TaskHandle_t worker_task_handle_ = nullptr;
    volatile bool running_           = false;
    int ringing_id_                  = -1;
};
