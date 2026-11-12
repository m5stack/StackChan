#pragma once

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

namespace agent_tasks {

enum class AgentStatus { Idle, Working, Waiting, Completed, Failed };
enum class TaskStatus { Pending, InProgress, Completed };
constexpr size_t kMaxTasks = 12;
constexpr size_t kMaxStageCodepoints = 24;
constexpr size_t kMaxTaskCodepoints = 64;

struct Task {
    std::string id;
    std::string text;
    TaskStatus status = TaskStatus::Pending;
};

struct AgentView {
    AgentStatus status = AgentStatus::Idle;
    std::string stage;
    std::vector<Task> tasks;
    bool online = false;
    bool stale = false;
};

struct Snapshot {
    AgentView codex;
    AgentView claude;
};

inline bool valid_utf8(const std::string& value) {
    for (size_t i = 0; i < value.size();) {
        const auto a = static_cast<unsigned char>(value[i]);
        const size_t n = a < 0x80 ? 1 : a >= 0xC2 && a <= 0xDF ? 2 :
                         a >= 0xE0 && a <= 0xEF ? 3 : a >= 0xF0 && a <= 0xF4 ? 4 : 0;
        if (!n || i + n > value.size()) return false;
        for (size_t j = 1; j < n; ++j) {
            const auto b = static_cast<unsigned char>(value[i + j]);
            if ((b & 0xC0) != 0x80) return false;
            if (j == 1 && ((a == 0xE0 && b < 0xA0) || (a == 0xED && b >= 0xA0) ||
                           (a == 0xF0 && b < 0x90) || (a == 0xF4 && b >= 0x90))) return false;
        }
        i += n;
    }
    return true;
}

inline size_t codepoints(const std::string& value) {
    size_t result = 0;
    for (unsigned char c : value) if ((c & 0xC0) != 0x80) ++result;
    return result;
}

inline const char* status_label(const AgentView& view) {
    if (view.stale) return "已过期";
    if (!view.online) return "离线";
    switch (view.status) {
        case AgentStatus::Idle: return "空闲";
        case AgentStatus::Working: return "工作中";
        case AgentStatus::Waiting: return "等待中";
        case AgentStatus::Completed: return "已完成";
        case AgentStatus::Failed: return "失败";
    }
    return "未知";
}

class Model {
public:
    bool replace(const Snapshot& next) {
        if (!valid(next.codex) || !valid(next.claude)) return false;
        snapshot_ = next;
        return true;
    }
    const Snapshot& snapshot() const { return snapshot_; }

private:
    static bool valid(const AgentView& view) {
        if (static_cast<int>(view.status) < 0 || static_cast<int>(view.status) > 4 ||
            view.tasks.size() > kMaxTasks || !valid_utf8(view.stage) ||
            codepoints(view.stage) > kMaxStageCodepoints) return false;
        for (const auto& task : view.tasks) {
            if (task.id.empty() || task.id.size() > 128 || !valid_utf8(task.text) ||
                codepoints(task.text) > kMaxTaskCodepoints ||
                static_cast<int>(task.status) < 0 || static_cast<int>(task.status) > 2) return false;
        }
        return true;
    }
    Snapshot snapshot_;
};

}  // namespace agent_tasks
