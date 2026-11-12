#pragma once
#include "agent_tasks_poller.h"
#include "view/dashboard_view.h"
#include <mooncake.h>
#include <memory>

class AppAgentTasks : public mooncake::AppAbility {
public:
    AppAgentTasks();
    void onOpen() override;
    void onRunning() override;
    void onClose() override;
private:
    agent_tasks::Poller poller_;
    std::unique_ptr<agent_tasks::DashboardView> view_;
    uint32_t last_update_ = 0;
};
