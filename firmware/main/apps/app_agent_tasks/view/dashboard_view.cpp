#include "dashboard_view.h"
#include <lvgl.h>

using namespace smooth_ui_toolkit::lvgl_cpp;
LV_FONT_DECLARE(agent_tasks_cjk_14);

namespace agent_tasks {
DashboardView::DashboardView() {
    root_ = std::make_unique<Container>(lv_screen_active());
    root_->setSize(320, 240); root_->setAlign(LV_ALIGN_CENTER); root_->setRadius(0);
    root_->setBgColor(lv_color_hex(0x10171E)); root_->setBorderWidth(0); root_->setPaddingAll(0);
    lv_obj_remove_flag(root_->get(), LV_OBJ_FLAG_SCROLLABLE);
    for (int i = 0; i < 2; ++i) {
        const auto accent = lv_color_hex(i ? 0xEBB37B : 0x72D6C1);
        columns_[i] = std::make_unique<Container>(root_->get());
        columns_[i]->setSize(154, 224); columns_[i]->setPos(i ? 162 : 4, 5);
        columns_[i]->setRadius(8); columns_[i]->setBgColor(lv_color_hex(i ? 0x25272C : 0x1A292A));
        columns_[i]->setBorderWidth(0); columns_[i]->setPadding(6, 5, 5, 5);
        lv_obj_remove_flag(columns_[i]->get(), LV_OBJ_FLAG_SCROLLABLE);
        title_[i] = std::make_unique<Label>(columns_[i]->get());
        title_[i]->setTextFont(&lv_font_montserrat_14); title_[i]->setTextColor(accent);
        title_[i]->setText(i ? LV_SYMBOL_EDIT "  CLAUDE" : LV_SYMBOL_KEYBOARD "  CODEX");
        title_[i]->setPos(0, 0);
        title_[i]->setSize(104, 20);
        title_[i]->setLongMode(LV_LABEL_LONG_MODE_DOTS);
        progress_[i] = std::make_unique<Label>(columns_[i]->get());
        progress_[i]->setTextFont(&lv_font_montserrat_14);
        progress_[i]->setTextColor(accent);
        progress_[i]->setSize(38, 20);
        progress_[i]->setPos(104, 0);
        progress_[i]->setText("--%");
        lv_obj_set_style_text_align(progress_[i]->get(), LV_TEXT_ALIGN_RIGHT, 0);
        state_[i] = std::make_unique<Label>(columns_[i]->get());
        state_[i]->setTextFont(&agent_tasks_cjk_14); state_[i]->setTextColor(lv_color_hex(0xE4EBEF));
        state_[i]->setSize(120, 21); state_[i]->setLongMode(LV_LABEL_LONG_MODE_DOTS);
        state_[i]->setPos(0, 24);
        spinners_[i] = lv_spinner_create(columns_[i]->get());
        lv_obj_set_size(spinners_[i], 16, 16);
        lv_obj_set_pos(spinners_[i], 124, 25);
        lv_obj_set_style_arc_width(spinners_[i], 2, LV_PART_MAIN);
        lv_obj_set_style_arc_width(spinners_[i], 2, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(spinners_[i], lv_color_hex(0x34434B), LV_PART_MAIN);
        lv_obj_set_style_arc_color(spinners_[i], accent, LV_PART_INDICATOR);
        lv_obj_add_flag(spinners_[i], LV_OBJ_FLAG_HIDDEN);
        lv_anim_delete(spinners_[i], nullptr);
        stage_[i] = std::make_unique<Label>(columns_[i]->get());
        stage_[i]->setTextFont(&agent_tasks_cjk_14); stage_[i]->setTextColor(lv_color_hex(0x95A5AD));
        stage_[i]->setSize(140, 21); stage_[i]->setLongMode(LV_LABEL_LONG_MODE_DOTS);
        stage_[i]->setPos(0, 45);
        task_panels_[i] = std::make_unique<Container>(columns_[i]->get());
        task_panels_[i]->setSize(142, 146); task_panels_[i]->setPos(0, 68);
        task_panels_[i]->setBgOpa(0); task_panels_[i]->setBorderWidth(0);
        task_panels_[i]->setPaddingAll(0); task_panels_[i]->setScrollDir(LV_DIR_VER);
        task_panels_[i]->setScrollbarMode(LV_SCROLLBAR_MODE_ACTIVE);
        for (size_t j = 0; j < kMaxTasks; ++j) {
            cards_[i][j] = std::make_unique<Container>(task_panels_[i]->get());
            auto& card = cards_[i][j];
            card->setSize(140, 46); card->setPos(0, j * 48); card->setRadius(5);
            card->setBgColor(lv_color_hex(0x121C23)); card->setBorderWidth(0); card->setPaddingAll(0);
            lv_obj_remove_flag(card->get(), LV_OBJ_FLAG_SCROLLABLE);
            markers_[i][j] = std::make_unique<Label>(card->get());
            markers_[i][j]->setTextFont(&lv_font_montserrat_14);
            markers_[i][j]->setTextColor(accent); markers_[i][j]->setPos(4, 6);
            tasks_[i][j] = std::make_unique<Label>(card->get());
            tasks_[i][j]->setTextFont(&agent_tasks_cjk_14);
            tasks_[i][j]->setTextColor(lv_color_hex(0xE2E8EB));
            tasks_[i][j]->setSize(116, 42); tasks_[i][j]->setPos(21, 2);
            tasks_[i][j]->setLongMode(LV_LABEL_LONG_MODE_DOTS);
            lv_obj_add_flag(card->get(), LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void DashboardView::update_column(int index, const AgentView& view, bool configured, bool connected) {
    const auto completion = completion_progress(view);
    const std::string percent = completion.total ? std::to_string(completion.percent) + "%" : "--%";
    if (rendered_progress_[index] != percent) {
        rendered_progress_[index] = percent;
        progress_[index]->setText(percent);
    }
    const bool live = configured && connected && view.online && !view.stale;
    const bool working = live && view.status == AgentStatus::Working;
    const bool waiting = live && view.status == AgentStatus::Waiting;
    const auto phase = static_cast<int>((lv_tick_get() / 600) % 4);
    if (spinning_[index] != working) {
        spinning_[index] = working;
        if (working) {
            lv_obj_remove_flag(spinners_[index], LV_OBJ_FLAG_HIDDEN);
            lv_spinner_set_anim_params(spinners_[index], 1000, 240);
        } else {
            lv_anim_delete(spinners_[index], nullptr);
            lv_obj_add_flag(spinners_[index], LV_OBJ_FLAG_HIDDEN);
        }
    }
    std::string state = !configured ? "请先配置网关" : !connected ? "网关未连接" : status_label(view);
    if (working || waiting) state += std::string(phase, '.');
    if (rendered_state_[index] != state) {
        rendered_state_[index] = state;
        state_[index]->setText(state);
    }
    const std::string stage = connected && !view.stage.empty() ? view.stage :
                              configured ? "状态与待办" : "SETUP / Apps";
    if (rendered_stage_[index] != stage) {
        rendered_stage_[index] = stage;
        stage_[index]->setText(stage);
    }
    // A light pulse draws attention to active rows without moving the text.
    for (size_t j = 0; j < std::min(view.tasks.size(), kMaxTasks); ++j) {
        const bool active = working && view.tasks[j].status == TaskStatus::InProgress;
        const int opacity = active && phase >= 2 ? LV_OPA_50 : LV_OPA_COVER;
        if (rendered_marker_opacity_[index][j] != opacity) {
            markers_[index][j]->setOpa(opacity);
            rendered_marker_opacity_[index][j] = opacity;
        }
    }
    std::string signature = view.tasks.empty() ? "empty" : "tasks";
    for (const auto& task : view.tasks)
        signature += std::to_string(task.text.size()) + ":" + task.text + std::to_string(static_cast<int>(task.status));
    if (rendered_tasks_[index] == signature) return;
    rendered_tasks_[index] = signature;
    const size_t count = std::max<size_t>(1, std::min(view.tasks.size(), kMaxTasks));
    for (size_t j = 0; j < kMaxTasks; ++j) {
        if (j >= count) { lv_obj_add_flag(cards_[index][j]->get(), LV_OBJ_FLAG_HIDDEN); continue; }
        lv_obj_remove_flag(cards_[index][j]->get(), LV_OBJ_FLAG_HIDDEN);
        if (view.tasks.empty()) {
            markers_[index][j]->setText(LV_SYMBOL_MINUS);
            markers_[index][j]->setOpa(LV_OPA_COVER);
            rendered_marker_opacity_[index][j] = LV_OPA_COVER;
            tasks_[index][j]->setText("暂无待办任务");
            tasks_[index][j]->setTextColor(lv_color_hex(0xE2E8EB));
        } else {
            const auto& task = view.tasks[j];
            markers_[index][j]->setText(task.status == TaskStatus::Completed ? LV_SYMBOL_OK :
                                       task.status == TaskStatus::InProgress ? LV_SYMBOL_PLAY : LV_SYMBOL_MINUS);
            tasks_[index][j]->setText(task.text);
            tasks_[index][j]->setTextColor(lv_color_hex(task.status == TaskStatus::Completed ? 0x8A9D9C : 0xE2E8EB));
        }
    }
}

void DashboardView::update(const Snapshot& snapshot, bool configured, bool connected) {
    update_column(0, snapshot.codex, configured, connected);
    update_column(1, snapshot.claude, configured, connected);
}
}
