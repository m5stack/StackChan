#include <ota.h>

#include <assets/lang_config.h>
#include <settings.h>
#include <system_info.h>

#include <cJSON.h>
#include <esp_app_format.h>
#include <esp_err.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>

#include <sys/time.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <vector>

#define TAG "Ota"

namespace {

constexpr size_t kOtaHeaderSize =
    sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t);

bool IsUpstreamOtaUrl(const std::string& url)
{
    return url.find("api.tenclass.net") != std::string::npos || url.find("xiaozhi") != std::string::npos;
}

bool ParseVersionSegment(const std::string& segment, int& value)
{
    if (segment.empty()) {
        return false;
    }
    for (char ch : segment) {
        if (!std::isdigit(static_cast<unsigned char>(ch))) {
            return false;
        }
    }
    value = std::stoi(segment);
    return true;
}

bool ValidateOtaImageHeader(const uint8_t* data, size_t size, std::string& error_message)
{
    if (size < kOtaHeaderSize) {
        error_message = "OTA image header is truncated";
        return false;
    }

    esp_image_header_t image_header = {};
    esp_app_desc_t app_desc = {};
    std::memcpy(&image_header, data, sizeof(image_header));
    std::memcpy(&app_desc, data + sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t), sizeof(app_desc));

    if (image_header.magic != ESP_IMAGE_HEADER_MAGIC) {
        error_message = "OTA image has invalid magic";
        return false;
    }

    if (app_desc.project_name[0] == '\0') {
        error_message = "OTA image is missing project name";
        return false;
    }

    const esp_app_desc_t* running_desc = esp_app_get_description();
    if (std::strncmp(app_desc.project_name, running_desc->project_name, sizeof(app_desc.project_name)) != 0) {
        error_message = "OTA image project name does not match running firmware";
        return false;
    }

    if (app_desc.version[0] == '\0') {
        error_message = "OTA image is missing version";
        return false;
    }

    return true;
}

}  // namespace

Ota::Ota() = default;

Ota::~Ota() = default;

std::string Ota::GetCheckVersionUrl()
{
    Settings settings("wifi", false);
    std::string url = settings.GetString("ota_url");
    if (url.empty()) {
        url = CONFIG_OTA_URL;
    }

    if (url.empty()) {
        return "";
    }

    if (IsUpstreamOtaUrl(url)) {
        ESP_LOGW(TAG, "Ignoring upstream OTA URL: %s", url.c_str());
        Settings writable("wifi", true);
        writable.EraseKey("ota_url");
        return "";
    }

    return url;
}

std::unique_ptr<Http> Ota::SetupHttp()
{
    auto& board = Board::GetInstance();
    auto http = board.GetNetwork()->CreateHttp(0);
    http->SetHeader("Device-Id", SystemInfo::GetMacAddress().c_str());
    http->SetHeader("Client-Id", board.GetUuid());
    http->SetHeader("User-Agent", SystemInfo::GetUserAgent());
    http->SetHeader("Accept-Language", Lang::CODE);
    http->SetHeader("Content-Type", "application/json");
    return http;
}

esp_err_t Ota::CheckVersion()
{
    current_version_ = esp_app_get_description()->version;
    firmware_version_.clear();
    firmware_url_.clear();
    has_new_version_ = false;
    has_server_time_ = false;
    has_activation_code_ = false;
    has_activation_challenge_ = false;

    std::string url = GetCheckVersionUrl();
    if (url.empty()) {
        ESP_LOGW(TAG, "OTA URL not configured");
        return ESP_ERR_NOT_FOUND;
    }

    auto http = SetupHttp();
    http->SetContent(Board::GetInstance().GetSystemInfoJson());
    const std::string method = http->GetBodyLength() > 0 ? "POST" : "GET";
    if (!http->Open(method, url)) {
        const int last_error = http->GetLastError();
        ESP_LOGE(TAG, "Failed to open OTA URL, code=0x%x", last_error);
        return last_error == 0 ? ESP_FAIL : last_error;
    }

    const int status_code = http->GetStatusCode();
    if (status_code != 200) {
        ESP_LOGE(TAG, "Unexpected OTA status code: %d", status_code);
        http->Close();
        return status_code;
    }

    std::string payload = http->ReadAll();
    http->Close();

    cJSON* root = cJSON_Parse(payload.c_str());
    if (root == nullptr) {
        ESP_LOGE(TAG, "Failed to parse OTA response");
        return ESP_ERR_INVALID_RESPONSE;
    }

    const cJSON* server_time = cJSON_GetObjectItemCaseSensitive(root, "server_time");
    if (cJSON_IsObject(server_time)) {
        const cJSON* timestamp = cJSON_GetObjectItemCaseSensitive(server_time, "timestamp");
        const cJSON* timezone_offset = cJSON_GetObjectItemCaseSensitive(server_time, "timezone_offset");
        if (cJSON_IsNumber(timestamp)) {
            double ts_ms = timestamp->valuedouble;
            if (cJSON_IsNumber(timezone_offset)) {
                ts_ms += static_cast<double>(timezone_offset->valueint) * 60.0 * 1000.0;
            }

            struct timeval tv = {
                .tv_sec = static_cast<time_t>(ts_ms / 1000.0),
                .tv_usec = static_cast<suseconds_t>(static_cast<long long>(ts_ms) % 1000) * 1000,
            };
            settimeofday(&tv, nullptr);
            has_server_time_ = true;
        }
    }

    const cJSON* firmware = cJSON_GetObjectItemCaseSensitive(root, "firmware");
    if (cJSON_IsObject(firmware)) {
        const cJSON* version = cJSON_GetObjectItemCaseSensitive(firmware, "version");
        const cJSON* firmware_url = cJSON_GetObjectItemCaseSensitive(firmware, "url");
        const cJSON* force = cJSON_GetObjectItemCaseSensitive(firmware, "force");

        if (cJSON_IsString(version)) {
            firmware_version_ = version->valuestring;
        }
        if (cJSON_IsString(firmware_url)) {
            firmware_url_ = firmware_url->valuestring;
        }

        if (!firmware_version_.empty() && !firmware_url_.empty()) {
            has_new_version_ = IsNewVersionAvailable(current_version_, firmware_version_);
            if (cJSON_IsNumber(force) && force->valueint == 1) {
                has_new_version_ = true;
            }
        }
    }

    if (cJSON_GetObjectItemCaseSensitive(root, "activation") != nullptr) {
        ESP_LOGW(TAG, "Ignoring activation section in OTA response");
    }
    if (cJSON_GetObjectItemCaseSensitive(root, "mqtt") != nullptr) {
        ESP_LOGW(TAG, "Ignoring mqtt section in OTA response");
    }
    if (cJSON_GetObjectItemCaseSensitive(root, "websocket") != nullptr) {
        ESP_LOGW(TAG, "Ignoring websocket section in OTA response");
    }

    cJSON_Delete(root);
    return ESP_OK;
}

void Ota::MarkCurrentVersionValid()
{
    const esp_partition_t* partition = esp_ota_get_running_partition();
    if (partition == nullptr || std::strcmp(partition->label, "factory") == 0) {
        return;
    }

    esp_ota_img_states_t state;
    if (esp_ota_get_state_partition(partition, &state) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get partition state");
        return;
    }

    if (state == ESP_OTA_IMG_PENDING_VERIFY) {
        ESP_LOGI(TAG, "Marking app valid");
        esp_ota_mark_app_valid_cancel_rollback();
    }
}

bool Ota::Upgrade(const std::string& firmware_url, std::function<void(int progress, size_t speed)> callback)
{
    ESP_LOGI(TAG, "Upgrading firmware from %s", firmware_url.c_str());
    if (IsUpstreamOtaUrl(firmware_url)) {
        ESP_LOGE(TAG, "Rejecting upstream OTA firmware URL: %s", firmware_url.c_str());
        return false;
    }

    const esp_partition_t* update_partition = esp_ota_get_next_update_partition(nullptr);
    if (update_partition == nullptr) {
        ESP_LOGE(TAG, "No OTA update partition available");
        return false;
    }

    auto http = Board::GetInstance().GetNetwork()->CreateHttp(0);
    if (!http->Open("GET", firmware_url)) {
        ESP_LOGE(TAG, "Failed to open firmware URL");
        return false;
    }

    if (http->GetStatusCode() != 200) {
        ESP_LOGE(TAG, "Unexpected firmware status code: %d", http->GetStatusCode());
        http->Close();
        return false;
    }

    const size_t content_length = http->GetBodyLength();
    if (content_length == 0) {
        ESP_LOGE(TAG, "Firmware response missing content length");
        http->Close();
        return false;
    }
    if (content_length < kOtaHeaderSize) {
        ESP_LOGE(TAG, "Firmware image is too small to contain a valid OTA header");
        http->Close();
        return false;
    }
    if (content_length > update_partition->size) {
        ESP_LOGE(TAG, "Firmware image size %u exceeds OTA partition size %lu", content_length, update_partition->size);
        http->Close();
        return false;
    }

    esp_ota_handle_t update_handle = 0;
    constexpr size_t kPageSize = 4096;
    char* buffer = static_cast<char*>(heap_caps_malloc(kPageSize, MALLOC_CAP_INTERNAL));
    if (buffer == nullptr) {
        ESP_LOGE(TAG, "Failed to allocate OTA buffer");
        http->Close();
        return false;
    }

    bool ota_started = false;
    size_t total_read = 0;
    size_t recent_read = 0;
    int64_t last_report_us = esp_timer_get_time();
    bool ok = true;
    std::vector<uint8_t> header_buffer;
    header_buffer.reserve(kPageSize);

    while (ok) {
        const int read_len = http->Read(buffer, kPageSize);
        if (read_len < 0) {
            ESP_LOGE(TAG, "Failed to read OTA stream: %d", read_len);
            ok = false;
            break;
        }

        const bool is_last_chunk = (read_len == 0);
        if (!is_last_chunk) {
            total_read += static_cast<size_t>(read_len);
            recent_read += static_cast<size_t>(read_len);
        }

        if (!ota_started && !is_last_chunk) {
            header_buffer.insert(header_buffer.end(), reinterpret_cast<uint8_t*>(buffer),
                                 reinterpret_cast<uint8_t*>(buffer) + read_len);
            if (header_buffer.size() >= kOtaHeaderSize) {
                std::string error_message;
                if (!ValidateOtaImageHeader(header_buffer.data(), header_buffer.size(), error_message)) {
                    ESP_LOGE(TAG, "%s", error_message.c_str());
                    ok = false;
                    break;
                }

                if (esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES, &update_handle) != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to begin OTA");
                    ok = false;
                    break;
                }
                ota_started = true;

                const esp_err_t err = esp_ota_write(update_handle, header_buffer.data(), header_buffer.size());
                if (err != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to write validated OTA header chunk: %s", esp_err_to_name(err));
                    ok = false;
                    break;
                }
                header_buffer.clear();
            }
        } else if (ota_started && !is_last_chunk) {
            const esp_err_t err = esp_ota_write(update_handle, buffer, static_cast<size_t>(read_len));
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to write OTA data: %s", esp_err_to_name(err));
                ok = false;
                break;
            }
        }

        const int64_t now_us = esp_timer_get_time();
        if (now_us - last_report_us >= 1000000 || is_last_chunk) {
            const int progress = static_cast<int>((total_read * 100) / content_length);
            if (callback) {
                callback(progress, recent_read);
            }
            recent_read = 0;
            last_report_us = now_us;
        }

        if (is_last_chunk) {
            break;
        }
    }

    http->Close();
    heap_caps_free(buffer);

    if (!ok) {
        if (ota_started) {
            esp_ota_abort(update_handle);
        }
        return false;
    }
    if (!ota_started) {
        ESP_LOGE(TAG, "OTA image ended before a valid header was received");
        return false;
    }
    if (total_read != content_length) {
        ESP_LOGE(TAG, "OTA stream length mismatch: read=%u expected=%u", total_read, content_length);
        esp_ota_abort(update_handle);
        return false;
    }

    if (esp_ota_end(update_handle) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to finalize OTA");
        esp_ota_abort(update_handle);
        return false;
    }

    if (esp_ota_set_boot_partition(update_partition) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set OTA boot partition");
        return false;
    }

    ESP_LOGI(TAG, "Firmware upgrade successful");
    return true;
}

bool Ota::StartUpgrade(std::function<void(int progress, size_t speed)> callback)
{
    return !firmware_url_.empty() && Upgrade(firmware_url_, std::move(callback));
}

std::vector<int> Ota::ParseVersion(const std::string& version)
{
    std::vector<int> numbers;
    std::stringstream stream(version);
    std::string segment;
    while (std::getline(stream, segment, '.')) {
        int value = 0;
        if (!ParseVersionSegment(segment, value)) {
            return {};
        }
        numbers.push_back(value);
    }
    return numbers;
}

bool Ota::IsNewVersionAvailable(const std::string& currentVersion, const std::string& newVersion)
{
    const std::vector<int> current = ParseVersion(currentVersion);
    const std::vector<int> newer = ParseVersion(newVersion);
    if (current.empty() || newer.empty()) {
        return false;
    }

    const size_t compare_len = std::min(current.size(), newer.size());
    for (size_t i = 0; i < compare_len; ++i) {
        if (newer[i] > current[i]) {
            return true;
        }
        if (newer[i] < current[i]) {
            return false;
        }
    }
    return newer.size() > current.size();
}

std::string Ota::GetActivationPayload()
{
    return "{}";
}

esp_err_t Ota::Activate()
{
    ESP_LOGW(TAG, "Activation is not supported in this firmware");
    return ESP_ERR_NOT_SUPPORTED;
}
