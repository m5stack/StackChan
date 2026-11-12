#include "apps/app_agent_tasks/agent_tasks_model.h"
#include <cassert>

int main() {
    agent_tasks::Snapshot snapshot;
    snapshot.codex.status = agent_tasks::AgentStatus::Working;
    snapshot.claude.status = agent_tasks::AgentStatus::Waiting;
    agent_tasks::Model model;
    assert(model.replace(snapshot));
    assert(model.snapshot().codex.status == agent_tasks::AgentStatus::Working);
    assert(model.snapshot().claude.status == agent_tasks::AgentStatus::Waiting);
    auto progress = agent_tasks::completion_progress(snapshot.codex);
    assert(progress.total == 0 && progress.percent == 0);
    snapshot.codex.tasks = {
        {"c1", "done", agent_tasks::TaskStatus::Completed},
        {"c2", "active", agent_tasks::TaskStatus::InProgress},
    };
    snapshot.claude.tasks = {
        {"a1", "pending", agent_tasks::TaskStatus::Pending},
        {"a2", "done", agent_tasks::TaskStatus::Completed},
    };
    progress = agent_tasks::completion_progress(snapshot.codex);
    assert(progress.completed == 1 && progress.total == 2 && progress.percent == 50);
    progress = agent_tasks::completion_progress(snapshot.claude);
    assert(progress.completed == 1 && progress.total == 2 && progress.percent == 50);
    snapshot.claude.tasks = {{"a1", "done", agent_tasks::TaskStatus::Completed}};
    progress = agent_tasks::completion_progress(snapshot.claude);
    assert(progress.completed == 1 && progress.total == 1 && progress.percent == 100);
    snapshot.codex.tasks.push_back({"c3", "done", agent_tasks::TaskStatus::Completed});
    progress = agent_tasks::completion_progress(snapshot.codex);
    assert(progress.completed == 2 && progress.total == 3 && progress.percent == 67);
    agent_tasks::Snapshot good;
    good.codex.stage = "coding";
    assert(model.replace(good));
    auto bad = good;
    bad.codex.tasks.resize(agent_tasks::kMaxTasks + 1);
    assert(!model.replace(bad));
    assert(model.snapshot().codex.stage == "coding");
    agent_tasks::AgentView view;
    view.tasks.push_back({"task-1", "保留任务", agent_tasks::TaskStatus::InProgress});
    assert(std::string(agent_tasks::status_label(view)) == "离线");
    assert(agent_tasks::codepoints(view.tasks.front().text) == 4u);
    view.stage = std::string("\xE7\x8C", 2);
    agent_tasks::Snapshot invalid_snapshot;
    invalid_snapshot.codex = view;
    assert(!model.replace(invalid_snapshot));
    return 0;
}
