#include "agent_tasks_config.h"
#include <nvs.h>
#if __has_include("agent_tasks_local_defaults.h")
#include "agent_tasks_local_defaults.h"
#else
#define AGENT_TASKS_DEFAULT_URL ""
#define AGENT_TASKS_DEFAULT_TOKEN ""
#endif

namespace agent_tasks {
namespace {
constexpr char kNamespace[] = "agent_tasks";
constexpr size_t kUrlMax = 255;
constexpr size_t kTokenMax = 256;

bool read_string(nvs_handle_t handle, const char* key, size_t max, std::string& value) {
    size_t size = 0;
    esp_err_t err = nvs_get_str(handle, key, nullptr, &size);
    if (err == ESP_ERR_NVS_NOT_FOUND) return true;
    if (err != ESP_OK || size == 0 || size > max + 1) return false;
    value.resize(size);
    if (nvs_get_str(handle, key, value.data(), &size) != ESP_OK) return false;
    value.pop_back();
    return true;
}
}

bool load_config(Config& config) {
    nvs_handle_t handle;
    auto err = nvs_open(kNamespace, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        config = {AGENT_TASKS_DEFAULT_URL, AGENT_TASKS_DEFAULT_TOKEN};
        return config.url.empty() || save_config(config);
    }
    if (err != ESP_OK) return false;
    Config loaded;
    const bool ok = read_string(handle, "url", kUrlMax, loaded.url) &&
                    read_string(handle, "token", kTokenMax, loaded.token);
    nvs_close(handle);
    if (!ok) return false;
    bool use_defaults = false;
    if (loaded.url.empty() && AGENT_TASKS_DEFAULT_URL[0]) {
        loaded.url = AGENT_TASKS_DEFAULT_URL;
        use_defaults = true;
    }
    if (loaded.token.empty() && AGENT_TASKS_DEFAULT_TOKEN[0]) {
        loaded.token = AGENT_TASKS_DEFAULT_TOKEN;
        use_defaults = true;
    }
    config = std::move(loaded);
    return !use_defaults || save_config(config);
}

bool save_config(const Config& config) {
    if (config.url.size() > kUrlMax || config.token.size() > kTokenMax ||
        config.url.find_first_of("\r\n") != std::string::npos ||
        config.token.find_first_of("\r\n") != std::string::npos) return false;
    nvs_handle_t handle;
    auto err = nvs_open(kNamespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) return false;
    err = nvs_set_str(handle, "url", config.url.c_str());
    if (err == ESP_OK) err = nvs_set_str(handle, "token", config.token.c_str());
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err == ESP_OK;
}
}
