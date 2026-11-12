#include "agent_tasks_poller.h"
#include <board.h>
#include <network_interface.h>
#include <wifi_manager.h>
#include <ssid_manager.h>
#include <esp_timer.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cJSON.h>
#include <algorithm>
#include <cstring>

namespace agent_tasks {
namespace {
constexpr int kTimeoutMs = 5000;
constexpr size_t kMaxResponse = 8192;

bool parse_agent(cJSON* object, const char* expected, AgentView& view) {
    auto* agent = cJSON_GetObjectItemCaseSensitive(object, "agent");
    auto* status = cJSON_GetObjectItemCaseSensitive(object, "status");
    auto* stage = cJSON_GetObjectItemCaseSensitive(object, "stage");
    auto* online = cJSON_GetObjectItemCaseSensitive(object, "online");
    auto* stale = cJSON_GetObjectItemCaseSensitive(object, "stale");
    auto* tasks = cJSON_GetObjectItemCaseSensitive(object, "tasks");
    if (!cJSON_IsString(agent) || strcmp(agent->valuestring, expected) || !cJSON_IsString(status) ||
        !cJSON_IsString(stage) || !cJSON_IsBool(online) || !cJSON_IsBool(stale) || !cJSON_IsArray(tasks) ||
        cJSON_GetArraySize(tasks) > static_cast<int>(kMaxTasks)) return false;
    const char* statuses[] = {"idle", "working", "waiting", "completed", "failed"};
    bool found = false;
    for (int i = 0; i < 5; ++i) if (!strcmp(status->valuestring, statuses[i])) {
        view.status = static_cast<AgentStatus>(i); found = true; break;
    }
    if (!found) return false;
    view.stage = stage->valuestring;
    view.online = cJSON_IsTrue(online);
    view.stale = cJSON_IsTrue(stale);
    for (auto* item = tasks->child; item; item = item->next) {
        auto* id = cJSON_GetObjectItemCaseSensitive(item, "id");
        auto* text = cJSON_GetObjectItemCaseSensitive(item, "text");
        auto* task_status = cJSON_GetObjectItemCaseSensitive(item, "status");
        if (!cJSON_IsString(id) || !cJSON_IsString(text) || !cJSON_IsString(task_status)) return false;
        Task task;
        task.id = id->valuestring;
        task.text = text->valuestring;
        if (!strcmp(task_status->valuestring, "pending")) task.status = TaskStatus::Pending;
        else if (!strcmp(task_status->valuestring, "in_progress")) task.status = TaskStatus::InProgress;
        else if (!strcmp(task_status->valuestring, "completed")) task.status = TaskStatus::Completed;
        else return false;
        view.tasks.push_back(std::move(task));
    }
    return true;
}

bool fetch(const Config& config, Snapshot& result, const std::atomic_bool& stop) {
    if (config.url.compare(0, 7, "http://") && config.url.compare(0, 8, "https://")) return false;
    // Mooncake does not start Wi-Fi at boot. Reuse the official network
    // manager and saved credentials before making the first HTTP request.
    auto& wifi = WifiManager::GetInstance();
    if (!wifi.IsConnected()) {
        if (SsidManager::GetInstance().GetSsidList().empty() || wifi.IsConfigMode()) return false;
        if (!wifi.IsInitialized()) Board::GetInstance().StartNetwork();
        else wifi.StartStation();
        const int64_t deadline = esp_timer_get_time() + int64_t(kTimeoutMs) * 1000;
        while (!wifi.IsConnected() && !stop && esp_timer_get_time() < deadline)
            vTaskDelay(pdMS_TO_TICKS(100));
        if (!wifi.IsConnected() || stop) return false;
    }
    std::string url = config.url;
    while (!url.empty() && url.back() == '/') url.pop_back();
    url += "/agent-dashboard";
    auto network = Board::GetInstance().GetNetwork();
    if (!network) return false;
    esp_log_level_set("HttpClient", ESP_LOG_WARN);
    auto client = network->CreateHttp(0);
    if (!client) return false;
    const int64_t started = esp_timer_get_time();
    auto set_remaining_timeout = [&]() {
        const int remaining = kTimeoutMs - static_cast<int>((esp_timer_get_time() - started) / 1000);
        if (remaining <= 0 || stop) return false;
        client->SetTimeout(remaining);
        return true;
    };
    if (!set_remaining_timeout()) return false;
    client->SetKeepAlive(false);
    client->SetHeader("Authorization", "Bearer " + config.token);
    client->SetHeader("Accept", "application/json");
    client->SetHeader("Connection", "close");
    bool ok = client->Open("GET", url) && set_remaining_timeout() && client->GetStatusCode() == 200;
    std::string body;
    char buffer[512];
    const size_t content_length = ok ? client->GetBodyLength() : 0;
    if (content_length == 0 || content_length > kMaxResponse) ok = false;
    while (ok && !stop && body.size() < content_length) {
        if (!set_remaining_timeout()) { ok = false; break; }
        const size_t remaining = std::min(sizeof(buffer), content_length - body.size());
        const auto read = client->Read(buffer, remaining);
        if (!read) { ok = false; break; }
        const int n = *read;
        if (n < 0 || body.size() + static_cast<size_t>(n) > kMaxResponse) { ok = false; break; }
        if (n == 0) { ok = false; break; }
        body.append(buffer, n);
    }
    client->Close();
    if (body.size() != content_length || esp_timer_get_time() - started >= int64_t(kTimeoutMs) * 1000) ok = false;
    if (!ok || stop) return false;
    auto* root = cJSON_ParseWithLength(body.c_str(), body.size());
    if (!root) return false;
    auto* agents = cJSON_GetObjectItemCaseSensitive(root, "agents");
    ok = cJSON_IsArray(agents) && cJSON_GetArraySize(agents) == 2;
    if (ok) ok = parse_agent(cJSON_GetArrayItem(agents, 0), "codex", result.codex) &&
                 parse_agent(cJSON_GetArrayItem(agents, 1), "claude", result.claude);
    cJSON_Delete(root);
    Model model;
    return ok && model.replace(result);
}
}

bool Poller::start(const Config& config) {
    stop_ = false; configured_ = !config.url.empty(); connected_ = false;
    if (!configured_) return true;
    struct StartArgs { Poller* self; Config config; };
    auto* args = new StartArgs{this, config};
    task_running_ = true;
    const auto created = xTaskCreate([](void* opaque) {
        std::unique_ptr<StartArgs> args(static_cast<StartArgs*>(opaque));
        auto* self = args->self;
        self->run(std::move(args->config));
        args.reset();
        self->task_running_ = false;
        vTaskDelete(nullptr);
    }, "agent_tasks", 6144, args, 2, nullptr);
    if (created != pdPASS) { delete args; task_running_ = false; return false; }
    return true;
}

void Poller::test_once(const Config& config) {
    Snapshot next;
    const bool ok = fetch(config, next, stop_);
    if (ok) {
        std::lock_guard<std::mutex> lock(mutex_);
        snapshot_ = std::move(next);
        connected_ = true;
    } else connected_ = false;
}

void Poller::run(Config config) {
    while (!stop_) {
        Snapshot next;
        if (fetch(config, next, stop_)) {
            std::lock_guard<std::mutex> lock(mutex_); snapshot_ = std::move(next); connected_ = true;
        } else connected_ = false;
        for (int i = 0; i < 10 && !stop_; ++i) vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void Poller::stop() {
    stop_ = true;
    while (task_running_) vTaskDelay(pdMS_TO_TICKS(10));
}
Snapshot Poller::latest() const { std::lock_guard<std::mutex> lock(mutex_); return snapshot_; }
}
