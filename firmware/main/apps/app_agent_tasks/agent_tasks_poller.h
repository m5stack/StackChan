#pragma once
#include "agent_tasks_model.h"
#include "agent_tasks_config.h"
#include <atomic>
#include <mutex>

namespace agent_tasks {
class Poller {
public:
    bool start(const Config& config);
    void stop();
    void test_once(const Config& config);
    Snapshot latest() const;
    bool configured() const { return configured_; }
    bool connected() const { return connected_; }
private:
    mutable std::mutex mutex_;
    Snapshot snapshot_;
    std::atomic_bool stop_{false};
    std::atomic_bool connected_{false};
    std::atomic_bool task_running_{false};
    bool configured_ = false;
    void run(Config config);
};
}
