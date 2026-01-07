/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "reminder.h"
#include <esp_log.h>
#include <assets/assets.h>
#include <hal/hal.h>
#include <hal/board/hal_bridge.h>

static const char* TAG = "ReminderManager";

ReminderItem::ReminderItem(int duration_s, const std::string& msg) : message_(msg)
{
    target_time_ = std::chrono::steady_clock::now() + std::chrono::seconds(duration_s);
}

bool ReminderItem::IsDue() const
{
    return std::chrono::steady_clock::now() >= target_time_;
}

ReminderManager& ReminderManager::GetInstance()
{
    static ReminderManager instance;
    return instance;
}

ReminderManager::ReminderManager()
{
    mutex_ = xSemaphoreCreateMutex();
}

ReminderManager::~ReminderManager()
{
    running_ = false;
    // 等待任务结束（简单处理，实际可能需要更复杂的同步）
    if (worker_task_handle_) {
        // vTaskDelete(worker_task_handle_); // 不建议直接删除，最好让任务自己退出
        // 这里我们假设任务会检测 running_ 并退出
        int timeout = 100;
        while (eTaskGetState(worker_task_handle_) != eDeleted && timeout-- > 0) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
    if (mutex_) {
        vSemaphoreDelete(mutex_);
    }
}

void ReminderManager::Start()
{
    if (running_) return;
    running_ = true;
    xTaskCreate(
        [](void* arg) {
            ReminderManager* mgr = (ReminderManager*)arg;
            mgr->WorkerThread();
            vTaskDelete(NULL);
        },
        "reminder_worker", 4096, this, 5, &worker_task_handle_);
    ESP_LOGI(TAG, "ReminderManager started");
}

int ReminderManager::CreateReminder(int duration_s, const std::string& message)
{
    xSemaphoreTake(mutex_, portMAX_DELAY);
    auto item = std::make_unique<ReminderItem>(duration_s, message);
    int id    = pool_.create(std::move(item));
    xSemaphoreGive(mutex_);
    ESP_LOGI(TAG, "Created reminder ID: %d, Duration: %ds, Msg: %s", id, duration_s, message.c_str());
    return id;
}

void ReminderManager::StopReminder(int id)
{
    xSemaphoreTake(mutex_, portMAX_DELAY);

    // 如果正在响铃的是这个提醒，停止播放
    if (id == ringing_id_) {
        if (hal_bridge::is_xiaozhi_mode()) {
        } else {
            // audio_player_.Stop();
        }
        ringing_id_ = -1;
    }

    // 标记销毁
    auto* item = pool_.get(id);
    if (item) {
        item->requestDestroy();
        ESP_LOGI(TAG, "Stopped reminder ID: %d", id);
    }

    // 立即清理（或者等待 WorkerThread 清理也可以，这里立即清理更及时）
    pool_.destroy(id);
    xSemaphoreGive(mutex_);
}

void ReminderManager::WorkerThread()
{
    std::vector<std::pair<int, std::string>> triggered_list;
    while (running_) {
        triggered_list.clear();

        {
            xSemaphoreTake(mutex_, portMAX_DELAY);

            // 1. 检查所有提醒
            pool_.forEach([&](ReminderItem* item, int id) {
                if (!item->IsTriggered() && item->IsDue()) {
                    item->SetTriggered(true);
                    triggered_list.push_back({id, item->GetMessage()});
                }
            });

            // 2. 清理已销毁的对象
            pool_.cleanup();
            xSemaphoreGive(mutex_);
        }

        // 3. 处理触发的提醒（在锁外执行，防止死锁）
        for (const auto& pair : triggered_list) {
            int id                 = pair.first;
            const std::string& msg = pair.second;

            ESP_LOGI(TAG, "Reminder triggered! ID: %d, Msg: %s", id, msg.c_str());

            // 更新响铃 ID
            {
                xSemaphoreTake(mutex_, portMAX_DELAY);
                // 再次检查对象是否存在（可能在触发前一瞬间被删除了）
                if (pool_.get(id) == nullptr) {
                    xSemaphoreGive(mutex_);
                    continue;
                }
                ringing_id_ = id;
                xSemaphoreGive(mutex_);
            }

            // 播放铃声 (循环)
            if (!OGG_CAMERA_SHUTTER.empty()) {
                if (hal_bridge::is_xiaozhi_mode()) {
                    hal_bridge::app_play_sound(OGG_CAMERA_SHUTTER);
                } else {
                    // audio_player_.Play(reinterpret_cast<const uint8_t*>(OGG_CAMERA_SHUTTER.data()),
                    //                    OGG_CAMERA_SHUTTER.size(), true);
                }
            } else {
                ESP_LOGW(TAG, "No ringtone data available");
            }

            // 发出信号
            GetHAL().onReminderTriggered.emit(id, msg);
        }

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
