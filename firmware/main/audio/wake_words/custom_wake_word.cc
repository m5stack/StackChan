#include "custom_wake_word.h"

#include "assets.h"

#include <cJSON.h>
#include <esp_log.h>
#include <esp_mn_models.h>
#include <esp_mn_speech_commands.h>

#include <algorithm>
#include <memory>

namespace {

constexpr const char* kTag = "CustomWakeWord";
constexpr size_t kHistoryFrames = 2000 / 30;
constexpr size_t kEncodeStackBytes = 4096 * 7;

using JsonPtr = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>;

std::string JsonString(cJSON* object, const char* key, const std::string& fallback)
{
    cJSON* item = cJSON_GetObjectItem(object, key);
    return cJSON_IsString(item) ? item->valuestring : fallback;
}

int JsonInt(cJSON* object, const char* key, int fallback)
{
    cJSON* item = cJSON_GetObjectItem(object, key);
    return cJSON_IsNumber(item) ? item->valueint : fallback;
}

float JsonFloat(cJSON* object, const char* key, float fallback)
{
    cJSON* item = cJSON_GetObjectItem(object, key);
    return cJSON_IsNumber(item) ? static_cast<float>(item->valuedouble) : fallback;
}

}  // namespace

CustomWakeWord::CustomWakeWord()
    : history_encoder_(kTag, kHistoryFrames, kEncodeStackBytes)
{
}

CustomWakeWord::~CustomWakeWord()
{
    running_ = false;
    if (model_data_ != nullptr && multinet_ != nullptr) {
        multinet_->destroy(model_data_);
        model_data_ = nullptr;
    }
    if (owns_models_ && models_ != nullptr) {
        esp_srmodel_deinit(models_);
        models_ = nullptr;
    }
}

bool CustomWakeWord::Initialize(AudioCodec* codec, srmodel_list_t* models_list)
{
    if (codec == nullptr) {
        ESP_LOGE(kTag, "Audio codec is required");
        return false;
    }

    codec_ = codec;
    commands_.clear();
    language_ = "cn";
    duration_ms_ = 3000;
    threshold_ = 0.2f;

    return LoadModels(models_list) && CreateMultinet() && ConfigureCommands();
}

void CustomWakeWord::Feed(const std::vector<int16_t>& data)
{
    if (!running_ || model_data_ == nullptr || data.empty()) {
        return;
    }

    std::lock_guard<std::mutex> lock(input_mutex_);
    if (!running_) {
        return;
    }
    AppendInput(data);
    ProcessBufferedInput();
}

void CustomWakeWord::OnWakeWordDetected(std::function<void(const std::string& wake_word)> callback)
{
    wake_word_detected_callback_ = std::move(callback);
}

void CustomWakeWord::Start()
{
    running_ = true;
}

void CustomWakeWord::Stop()
{
    running_ = false;
    std::lock_guard<std::mutex> lock(input_mutex_);
    input_buffer_.clear();
}

size_t CustomWakeWord::GetFeedSize()
{
    return model_data_ != nullptr ? multinet_->get_samp_chunksize(model_data_) : 0;
}

void CustomWakeWord::EncodeWakeWordData()
{
    history_encoder_.Start();
}

bool CustomWakeWord::GetWakeWordOpus(std::vector<uint8_t>& opus)
{
    return history_encoder_.Pop(opus);
}

bool CustomWakeWord::LoadModels(srmodel_list_t* models_list)
{
    owns_models_ = false;
    models_ = models_list;
    if (models_ == nullptr) {
        models_ = esp_srmodel_init("model");
        owns_models_ = true;
#ifdef CONFIG_CUSTOM_WAKE_WORD
        threshold_ = CONFIG_CUSTOM_WAKE_WORD_THRESHOLD / 100.0f;
        commands_.push_back({1, CONFIG_CUSTOM_WAKE_WORD, CONFIG_CUSTOM_WAKE_WORD_DISPLAY, "wake"});
#endif
    } else if (!LoadAssetConfig()) {
        return false;
    }

    if (models_ == nullptr || models_->num <= 0) {
        ESP_LOGE(kTag, "No speech-recognition models available");
        return false;
    }
    if (commands_.empty()) {
        ESP_LOGE(kTag, "No custom wake-word commands configured");
        return false;
    }
    return true;
}

bool CustomWakeWord::LoadAssetConfig()
{
    void* data = nullptr;
    size_t size = 0;
    if (!Assets::GetInstance().GetAssetData("index.json", data, size)) {
        ESP_LOGE(kTag, "Missing index.json asset");
        return false;
    }

    JsonPtr root(cJSON_ParseWithLength(static_cast<const char*>(data), size), cJSON_Delete);
    if (root == nullptr) {
        ESP_LOGE(kTag, "Invalid index.json asset");
        return false;
    }

    cJSON* config = cJSON_GetObjectItem(root.get(), "multinet_model");
    if (!cJSON_IsObject(config)) {
        ESP_LOGE(kTag, "index.json does not contain multinet_model");
        return false;
    }

    language_ = JsonString(config, "language", language_);
    duration_ms_ = JsonInt(config, "duration", duration_ms_);
    threshold_ = JsonFloat(config, "threshold", threshold_);

    cJSON* commands = cJSON_GetObjectItem(config, "commands");
    if (!cJSON_IsArray(commands)) {
        ESP_LOGE(kTag, "multinet_model.commands must be an array");
        return false;
    }

    const int count = cJSON_GetArraySize(commands);
    commands_.reserve(count);
    for (int i = 0; i < count; ++i) {
        cJSON* item = cJSON_GetArrayItem(commands, i);
        if (!cJSON_IsObject(item)) {
            continue;
        }

        Command command = {
            .id = static_cast<int>(commands_.size()) + 1,
            .phrase = JsonString(item, "command", ""),
            .display_text = JsonString(item, "text", ""),
            .action = JsonString(item, "action", ""),
        };
        if (!command.phrase.empty() && !command.display_text.empty() && !command.action.empty()) {
            commands_.push_back(std::move(command));
        }
    }
    return !commands_.empty();
}

bool CustomWakeWord::CreateMultinet()
{
    model_name_ = esp_srmodel_filter(models_, ESP_MN_PREFIX, language_.c_str());
    if (model_name_ == nullptr) {
        ESP_LOGW(kTag, "Multinet model for '%s' not found; using first available model", language_.c_str());
        model_name_ = esp_srmodel_filter(models_, ESP_MN_PREFIX, nullptr);
    }
    if (model_name_ == nullptr) {
        ESP_LOGE(kTag, "No Multinet model available");
        return false;
    }

    multinet_ = esp_mn_handle_from_name(model_name_);
    if (multinet_ == nullptr) {
        ESP_LOGE(kTag, "Failed to load Multinet model: %s", model_name_);
        return false;
    }

    model_data_ = multinet_->create(model_name_, duration_ms_);
    if (model_data_ == nullptr) {
        ESP_LOGE(kTag, "Failed to create Multinet runtime");
        return false;
    }
    multinet_->set_det_threshold(model_data_, threshold_);
    return true;
}

bool CustomWakeWord::ConfigureCommands()
{
    esp_mn_commands_clear();
    for (const Command& command : commands_) {
        if (esp_mn_commands_add(command.id, command.phrase.c_str()) != ESP_OK) {
            ESP_LOGE(kTag, "Failed to add wake-word command: %s", command.phrase.c_str());
            return false;
        }
    }

    esp_mn_error_t* result = esp_mn_commands_update();
    if (result != nullptr) {
        ESP_LOGW(kTag, "Wake-word command update reported errors");
    }
    multinet_->print_active_speech_commands(model_data_);
    return true;
}

void CustomWakeWord::AppendInput(const std::vector<int16_t>& data)
{
    if (codec_->input_channels() == 2) {
        input_buffer_.reserve(input_buffer_.size() + data.size() / 2);
        for (size_t i = 0; i < data.size(); i += 2) {
            input_buffer_.push_back(data[i]);
        }
    } else {
        input_buffer_.insert(input_buffer_.end(), data.begin(), data.end());
    }
}

void CustomWakeWord::ProcessBufferedInput()
{
    const int chunk_samples = multinet_->get_samp_chunksize(model_data_);
    while (chunk_samples > 0 && static_cast<int>(input_buffer_.size()) >= chunk_samples && running_) {
        std::vector<int16_t> chunk(input_buffer_.begin(), input_buffer_.begin() + chunk_samples);
        history_encoder_.Append(chunk);

        const esp_mn_state_t state = multinet_->detect(model_data_, chunk.data());
        if (state == ESP_MN_STATE_DETECTED) {
            HandleDetection();
            multinet_->clean(model_data_);
            return;
        }
        if (state == ESP_MN_STATE_TIMEOUT) {
            multinet_->clean(model_data_);
        }

        input_buffer_.erase(input_buffer_.begin(), input_buffer_.begin() + chunk_samples);
    }
}

void CustomWakeWord::HandleDetection()
{
    esp_mn_results_t* results = multinet_->get_results(model_data_);
    if (results == nullptr) {
        return;
    }

    for (int i = 0; i < results->num; ++i) {
        const int command_id = results->command_id[i];
        auto match = std::find_if(commands_.begin(), commands_.end(), [command_id](const Command& command) {
            return command.id == command_id;
        });
        if (match == commands_.end()) {
            ESP_LOGW(kTag, "Ignoring unknown wake-word command id: %d", command_id);
            continue;
        }

        ESP_LOGI(kTag, "Custom wake word detected: id=%d phrase=%s probability=%f",
                 command_id,
                 results->string != nullptr ? results->string : "<null>",
                 results->prob[i]);
        if (match->action != "wake") {
            continue;
        }

        last_detected_wake_word_ = match->display_text;
        running_ = false;
        input_buffer_.clear();
        if (wake_word_detected_callback_ != nullptr) {
            wake_word_detected_callback_(last_detected_wake_word_);
        }
        return;
    }
}
