/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <hal/hal.h>
#include <functional>
#include <vector>

namespace stackchan {

class Timer {
public:
    using Callback = std::function<void(void)>;

    struct Task {
        uint32_t intervalMs;  // Time interval between executions (ms)
        uint32_t lastTick;    // Last tick recorded
        uint32_t delayMs;     // Delay before first execution (ms)
        int repeatCount;      // Remaining repeat count (-1 = infinite)
        Callback callback;    // Task callback function
        bool enabled;         // Enabled or disabled
        bool started;         // Has delay finished and started interval execution?
    };

    /**
     * @brief Register a new task
     * @param intervalMs Execution interval in milliseconds
     * @param repeatCount Times to execute (-1 = infinite loop)
     * @param delayMs Delay before the first execution (ms)
     * @param cb Callback function
     * @param start Whether to start immediately
     * @return Task ID
     */
    int addTask(uint32_t intervalMs, int repeatCount, uint32_t delayMs, Callback cb, bool start = true)
    {
        int id = -1;

        if (!_free_indices.empty()) {
            id = _free_indices.back();
            _free_indices.pop_back();
        } else {
            id = (int)_tasks.size();
            _tasks.push_back(Task());
        }

        Task& t       = _tasks[id];
        t.intervalMs  = intervalMs;
        t.repeatCount = repeatCount;
        t.delayMs     = delayMs;
        t.callback    = cb;
        t.enabled     = start;
        t.started     = false;
        t.lastTick    = GetHAL().millis();

        return id;
    }

    void setInterval(int id, uint32_t intervalMs)
    {
        if (is_valid(id)) {
            _tasks[id].intervalMs = intervalMs;
        }
    }

    void setRepeat(int id, int repeatCount)
    {
        if (is_valid(id)) {
            _tasks[id].repeatCount = repeatCount;
        }
    }

    void setDelay(int id, uint32_t delayMs)
    {
        if (is_valid(id)) {
            _tasks[id].delayMs = delayMs;
        }
    }

    void enable(int id)
    {
        if (is_valid(id)) {
            _tasks[id].enabled = true;
        }
    }

    void disable(int id)
    {
        if (is_valid(id)) {
            _tasks[id].enabled = false;
        }
    }

    /**
     * @brief Remove task and recycle its ID
     */
    void remove(int id)
    {
        if (is_valid(id)) {
            _tasks[id]          = Task();  // Reset content
            _tasks[id].enabled  = false;
            _tasks[id].callback = nullptr;
            _free_indices.push_back(id);  // Recycle index
        }
    }

    /**
     * @brief MUST be called periodically in your main loop
     */
    void update()
    {
        uint32_t now = GetHAL().millis();

        for (int i = 0; i < (int)_tasks.size(); i++) {
            Task& t = _tasks[i];
            if (!t.enabled) {
                continue;
            }
            if (!t.callback) {
                continue;
            }

            // Handle delay start
            if (!t.started) {
                if ((now - t.lastTick) >= t.delayMs) {
                    t.started  = true;
                    t.lastTick = now;
                } else {
                    continue;
                }
            }

            // Handle interval execution
            if ((now - t.lastTick) >= t.intervalMs) {
                t.lastTick = now;
                t.callback();

                if (t.repeatCount > 0) {
                    t.repeatCount--;
                    if (t.repeatCount == 0) {
                        t.enabled = false;
                    }
                }
            }
        }
    }

private:
    bool is_valid(int id)
    {
        if (id < 0) {
            return false;
        }
        if (id >= (int)_tasks.size()) {
            return false;
        }
        return true;
    }

    std::vector<Task> _tasks;
    std::vector<int> _free_indices;
};

}  // namespace stackchan
