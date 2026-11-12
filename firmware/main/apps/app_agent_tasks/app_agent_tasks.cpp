#include "app_agent_tasks.h"
#include <apps/common/home_indicator/home_indicator.h>
#include <assets/assets.h>
#include <hal/hal.h>
#include <optional>

AppAgentTasks::AppAgentTasks() {
    setAppInfo().name = "AGENT TASKS";
    static auto icon = assets::get_image("icon_ai_agent.bin");
    setAppInfo().icon = (void*)&icon;
    static uint32_t color = 0x58BFA8;
    setAppInfo().userData = (void*)&color;
}

void AppAgentTasks::onOpen() {
    agent_tasks::Config config;
    agent_tasks::load_config(config);
    poller_.start(config);
    LvglLockGuard lock;
    view_ = std::make_unique<agent_tasks::DashboardView>();
    view::create_home_indicator([&]() { close(); }, 0x58BFA8, 0x174B45);
    last_update_ = last_data_update_ = 0;
}

void AppAgentTasks::onRunning() {
    const auto now = GetHAL().millis();
    if (now - last_update_ < 33) return;
    last_update_ = now;
    std::optional<agent_tasks::Snapshot> snapshot;
    if (now - last_data_update_ >= 500) {
        snapshot = poller_.latest();
        last_data_update_ = now;
    }
    LvglLockGuard lock;
    view::update_home_indicator();
    if (view_ && snapshot) view_->update(*snapshot, poller_.configured(), poller_.connected());
}

void AppAgentTasks::onClose() {
    poller_.stop();
    LvglLockGuard lock;
    view_.reset();
    view::destroy_home_indicator();
}
