#include "app_agent_tasks.h"
#include <apps/common/home_indicator/home_indicator.h>
#include <assets/assets.h>
#include <hal/hal.h>

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
    last_update_ = 0;
}

void AppAgentTasks::onRunning() {
    if (GetHAL().millis() - last_update_ < 250) return;
    last_update_ = GetHAL().millis();
    auto snapshot = poller_.latest();
    LvglLockGuard lock;
    if (view_) view_->update(snapshot, poller_.configured(), poller_.connected());
    view::update_home_indicator();
}

void AppAgentTasks::onClose() {
    poller_.stop();
    LvglLockGuard lock;
    view_.reset();
    view::destroy_home_indicator();
}
