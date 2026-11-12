#pragma once
#include "../agent_tasks_model.h"
#include <memory>
#include <smooth_lvgl.hpp>
#include <uitk/short_namespace.hpp>

namespace agent_tasks {
class DashboardView {
public:
    DashboardView();
    void update(const Snapshot& snapshot, bool configured, bool connected);
private:
    std::unique_ptr<uitk::lvgl_cpp::Container> root_;
    std::unique_ptr<uitk::lvgl_cpp::Container> columns_[2], task_panels_[2];
    std::unique_ptr<uitk::lvgl_cpp::Label> title_[2], state_[2], stage_[2];
    lv_obj_t* spinners_[2] = {};
    bool spinning_[2] = {};
    std::unique_ptr<uitk::lvgl_cpp::Container> cards_[2][kMaxTasks];
    std::unique_ptr<uitk::lvgl_cpp::Label> markers_[2][kMaxTasks], tasks_[2][kMaxTasks];
    std::string rendered_state_[2], rendered_stage_[2], rendered_tasks_[2];
    void update_column(int index, const AgentView& view, bool configured, bool connected);
};
}
