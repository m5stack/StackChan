#include "afe_custom_wake_word.h"

#include "assets.h"

#include <cJSON.h>
#include <esp_afe_config.h>
#include <esp_log.h>
#include <esp_mn_models.h>
#include <esp_mn_speech_commands.h>

#include <algorithm>
#include <memory>

namespace {

constexpr const char* kTag = "AfeCustomWakeWord";
constexpr size_t kHistoryFrames = 2000 / 30;
constexpr size_t kEncodeStackBytes = 4096 * 7;
constexpr TickType_t kDetectionPollTicks = pdMS_TO_TICKS(100);
constexpr TickType_t kIdlePollTicks = pdMS_TO_TICKS(50);

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

AfeCustomWakeWord::AfeCustomWakeWord()
    : history_encoder_(kTag, kHistoryFrames, kEncodeStackBytes)
{
}

AfeCustomWakeWord::~AfeCustomWakeWord()
{
    stop_task_ = true;
    running_ = false;
    if (detection_task_ != nullptr) {
        vTaskDelete(detection_task_);
        detection_task_ = nullptr;
    }
    if (afe_data_ != nullptr && afe_iface_ != nullptr) {
        afe_iface_->destroy(afe_data_);
        afe_data_ = nullptr;
    }
    if (model_data_ != nullptr && multinet_ != nullptr) {
        multinet_->destroy(model_data_);
        model_data_ = nullptr;
    }
    if (owns_models_ && models_ != nullptr) {
        esp_srmodel_deinit(models_);
        models_ = nullptr;
    }
}

bool AfeCustomWakeWord::Initialize(AudioCodec* codec, srmodel_list_t* models_list)
{
    if (codec == nullptr) {
        ESP_LOGE(kTag, "Audio codec is required");
        return false;
    }

    codec_ = codec;

    if (!LoadModels(models_list) || !CreateMultinet() || !ConfigureCommands() || !CreateAfe()) {
        return false;
    }

    stop_task_ = false;
    if (xTaskCreate(DetectionTaskEntry, "afe_custom_wake", 4096, this, 3, &detection_task_) != pdPASS) {
        ESP_LOGE(kTag, "Failed to start detection task");
        return false;
    }
    return true;
}

void AfeCustomWakeWord::Feed(const std::vector<int16_t>& data)
{
    if (!running_ || afe_data_ == nullptr || codec_ == nullptr || data.empty()) {
        return;
    }

    std::lock_guard<std::mutex> lock(input_mutex_);
    if (!running_) {
        return;
    }

    input_buffer_.insert(input_buffer_.end(), data.begin(), data.end());
    const size_t chunk_samples =
        static_cast<size_t>(afe_iface_->get_feed_chunksize(afe_data_)) * codec_->input_channels();
    while (chunk_samples > 0 && input_buffer_.size() >= chunk_samples) {
        afe_iface_->feed(afe_data_, input_buffer_.data());
        input_buffer_.erase(input_buffer_.begin(), input_buffer_.begin() + chunk_samples);
    }
}

void AfeCustomWakeWord::OnWakeWordDetected(std::function<void(const std::string& wake_word)> callback)
{
    wake_word_detected_callback_ = std::move(callback);
}

void AfeCustomWakeWord::Start()
{
    if (model_data_ != nullptr && multinet_ != nullptr) {
        multinet_->clean(model_data_);
    }
    running_ = true;
}

void AfeCustomWakeWord::Stop()
{
    running_ = false;
    {
        std::lock_guard<std::mutex> lock(input_mutex_);
        input_buffer_.clear();
        if (afe_data_ != nullptr && afe_iface_ != nullptr) {
            afe_iface_->reset_buffer(afe_data_);
        }
    }
    mn_buffer_.clear();
    if (model_data_ != nullptr && multinet_ != nullptr) {
        multinet_->clean(model_data_);
    }
}

size_t AfeCustomWakeWord::GetFeedSize()
{
    return afe_data_ != nullptr ? static_cast<size_t>(afe_iface_->get_feed_chunksize(afe_data_)) : 0;
}

void AfeCustomWakeWord::EncodeWakeWordData()
{
    history_encoder_.Start();
}

bool AfeCustomWakeWord::GetWakeWordOpus(std::vector<uint8_t>& opus)
{
    return history_encoder_.Pop(opus);
}

bool AfeCustomWakeWord::LoadModels(srmodel_list_t* models_list)
{
    owns_models_ = false;
    models_ = models_list;
    commands_.clear();
    language_ = "cn";
    duration_ms_ = 3000;
    threshold_ = 0.2f;

    if (models_ == nullptr) {
        models_ = esp_srmodel_init("model");
        owns_models_ = true;
    } else {
        LoadAssetConfig();
    }

#ifdef CONFIG_CUSTOM_WAKE_WORD
    if (commands_.empty()) {
        ESP_LOGI(kTag, "No multinet_model.commands in assets; using Kconfig fallback");
        threshold_ = CONFIG_CUSTOM_WAKE_WORD_THRESHOLD / 100.0f;
        commands_.push_back({1, CONFIG_CUSTOM_WAKE_WORD, CONFIG_CUSTOM_WAKE_WORD_DISPLAY, "wake"});
    }
#endif

    ns_model_name_ = esp_srmodel_filter(models_, ESP_NSNET_PREFIX, nullptr);

    if (models_ == nullptr || models_->num <= 0) {
        ESP_LOGE(kTag, "No speech-recognition models available");
        return false;
    }
    if (commands_.empty()) {
        ESP_LOGE(kTag, "No wake-word commands configured");
        return false;
    }
    return true;
}

bool AfeCustomWakeWord::LoadAssetConfig()
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

    cJSON* cmds = cJSON_GetObjectItem(config, "commands");
    if (!cJSON_IsArray(cmds)) {
        ESP_LOGE(kTag, "multinet_model.commands must be an array");
        return false;
    }

    const int count = cJSON_GetArraySize(cmds);
    commands_.reserve(count);
    for (int i = 0; i < count; ++i) {
        cJSON* item = cJSON_GetArrayItem(cmds, i);
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

bool AfeCustomWakeWord::CreateAfe()
{
    const std::string input_format = BuildInputFormat();
    afe_config_t* config = afe_config_init(input_format.c_str(), nullptr, AFE_TYPE_VC, AFE_MODE_HIGH_PERF);
    if (config == nullptr) {
        ESP_LOGE(kTag, "Failed to allocate AFE config");
        return false;
    }

    config->aec_init = codec_->input_reference();
    config->aec_mode = AEC_MODE_VOIP_HIGH_PERF;
    config->agc_init = false;
    config->vad_init = false;
    config->memory_alloc_mode = AFE_MEMORY_ALLOC_MORE_PSRAM;
    config->afe_perferred_core = 1;
    config->afe_perferred_priority = 1;

    if (ns_model_name_ != nullptr) {
        config->ns_init = true;
        config->ns_model_name = ns_model_name_;
        config->afe_ns_mode = AFE_NS_MODE_NET;
        ESP_LOGI(kTag, "NS model: %s", ns_model_name_);
    } else {
        config->ns_init = false;
        ESP_LOGW(kTag, "No NS model found; noise suppression disabled");
    }

    afe_iface_ = esp_afe_handle_from_config(config);
    afe_data_ = afe_iface_ != nullptr ? afe_iface_->create_from_config(config) : nullptr;
    afe_config_free(config);

    if (afe_iface_ == nullptr || afe_data_ == nullptr) {
        ESP_LOGE(kTag, "Failed to create AFE");
        return false;
    }
    return true;
}

bool AfeCustomWakeWord::CreateMultinet()
{
    model_name_ = esp_srmodel_filter(models_, ESP_MN_PREFIX, language_.c_str());
    if (model_name_ == nullptr) {
        ESP_LOGW(kTag, "MultiNet model for '%s' not found; using first available", language_.c_str());
        model_name_ = esp_srmodel_filter(models_, ESP_MN_PREFIX, nullptr);
    }
    if (model_name_ == nullptr) {
        ESP_LOGE(kTag, "No MultiNet model available");
        return false;
    }

    multinet_ = esp_mn_handle_from_name(model_name_);
    if (multinet_ == nullptr) {
        ESP_LOGE(kTag, "Failed to load MultiNet model: %s", model_name_);
        return false;
    }

    model_data_ = multinet_->create(model_name_, duration_ms_);
    if (model_data_ == nullptr) {
        ESP_LOGE(kTag, "Failed to create MultiNet runtime");
        return false;
    }
    multinet_->set_det_threshold(model_data_, threshold_);
    ESP_LOGI(kTag, "MultiNet model=%s language=%s duration=%dms threshold=%.2f chunk_samples=%d",
             model_name_, language_.c_str(), duration_ms_,
             static_cast<double>(threshold_),
             multinet_->get_samp_chunksize(model_data_));
    return true;
}

bool AfeCustomWakeWord::ConfigureCommands()
{
    esp_mn_commands_clear();
#if CONFIG_SR_MN_EN_MULTINET5_SINGLE_RECOGNITION_QUANT8
    ESP_LOGI(kTag, "Registering commands using phoneme mode");
#else
    ESP_LOGI(kTag, "Registering commands using literal command mode");
#endif
    for (const Command& command : commands_) {
#if CONFIG_SR_MN_EN_MULTINET5_SINGLE_RECOGNITION_QUANT8
        const esp_err_t result =
            esp_mn_commands_phoneme_add(command.id, command.display_text.c_str(), command.phrase.c_str());
#else
        const esp_err_t result = esp_mn_commands_add(command.id, command.phrase.c_str());
#endif
        if (result != ESP_OK) {
            ESP_LOGE(kTag, "Failed to add command: text=%s phrase=%s",
                     command.display_text.c_str(), command.phrase.c_str());
            return false;
        }
        ESP_LOGI(kTag, "Command id=%d text=%s phrase=%s action=%s",
                 command.id, command.display_text.c_str(),
                 command.phrase.c_str(), command.action.c_str());
    }

    esp_mn_error_t* err = esp_mn_commands_update();
    if (err != nullptr) {
        ESP_LOGW(kTag, "Command update reported errors");
    }
    multinet_->print_active_speech_commands(model_data_);
    return true;
}

std::string AfeCustomWakeWord::BuildInputFormat() const
{
    const int input_channels = std::max(1, codec_->input_channels());
    const int ref_channels = codec_->input_reference() ? 1 : 0;
    const int mic_channels = std::max(1, input_channels - ref_channels);

    std::string format;
    format.append(static_cast<size_t>(mic_channels), 'M');
    format.append(static_cast<size_t>(ref_channels), 'R');
    return format;
}

void AfeCustomWakeWord::DetectionTaskEntry(void* arg)
{
    auto* self = static_cast<AfeCustomWakeWord*>(arg);
    self->DetectionLoop();
    self->detection_task_ = nullptr;
    vTaskDelete(nullptr);
}

void AfeCustomWakeWord::DetectionLoop()
{
    const int mn_chunk_samples = multinet_->get_samp_chunksize(model_data_);

    while (!stop_task_) {
        if (!running_) {
            vTaskDelay(kIdlePollTicks);
            continue;
        }

        afe_fetch_result_t* result = afe_iface_->fetch_with_delay(afe_data_, kDetectionPollTicks);
        if (result == nullptr || result->ret_value == ESP_FAIL) {
            continue;
        }

        const int frame_samples = result->data_size / sizeof(int16_t);
        history_encoder_.Append(result->data, frame_samples);
        mn_buffer_.insert(mn_buffer_.end(), result->data, result->data + frame_samples);

        while (static_cast<int>(mn_buffer_.size()) >= mn_chunk_samples) {
            std::vector<int16_t> chunk(mn_buffer_.begin(), mn_buffer_.begin() + mn_chunk_samples);
            mn_buffer_.erase(mn_buffer_.begin(), mn_buffer_.begin() + mn_chunk_samples);

            const esp_mn_state_t state = multinet_->detect(model_data_, chunk.data());
            if (state == ESP_MN_STATE_DETECTED) {
                HandleDetection();
                multinet_->clean(model_data_);
                mn_buffer_.clear();
                break;
            }
            if (state == ESP_MN_STATE_TIMEOUT) {
                multinet_->clean(model_data_);
            }
        }
    }
}

void AfeCustomWakeWord::HandleDetection()
{
    esp_mn_results_t* results = multinet_->get_results(model_data_);
    if (results == nullptr) {
        return;
    }

    for (int i = 0; i < results->num; ++i) {
        const int command_id = results->command_id[i];
        auto match = std::find_if(commands_.begin(), commands_.end(),
                                  [command_id](const Command& c) { return c.id == command_id; });
        if (match == commands_.end()) {
            ESP_LOGW(kTag, "Unknown command id: %d", command_id);
            continue;
        }

        ESP_LOGI(kTag, "Detected: id=%d phrase=%s probability=%f",
                 command_id,
                 results->string != nullptr ? results->string : "<null>",
                 results->prob[i]);
        if (match->action != "wake") {
            continue;
        }

        last_detected_wake_word_ = match->display_text;
        running_ = false;
        {
            std::lock_guard<std::mutex> lock(input_mutex_);
            input_buffer_.clear();
            afe_iface_->reset_buffer(afe_data_);
        }
        if (wake_word_detected_callback_ != nullptr) {
            wake_word_detected_callback_(last_detected_wake_word_);
        }
        return;
    }
}
