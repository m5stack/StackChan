#include "board.h"

#include <esp_chip_info.h>
#include <esp_log.h>
#include <esp_ota_ops.h>
#include <esp_random.h>

#include "assets/lang_config.h"
#include "display.h"
#include "firmware_identity.h"
#include "settings.h"
#include "system_info.h"

namespace {

constexpr char kTag[] = "Board";

struct JsonDeleter {
    void operator()(cJSON* item) const
    {
        if (item != nullptr) {
            cJSON_Delete(item);
        }
    }
};

using JsonPtr = std::unique_ptr<cJSON, JsonDeleter>;

JsonPtr CreateObject()
{
    JsonPtr object(cJSON_CreateObject());
    if (object == nullptr) {
        ESP_LOGE(kTag, "Failed to allocate JSON object");
    }
    return object;
}

JsonPtr CreateArray()
{
    JsonPtr array(cJSON_CreateArray());
    if (array == nullptr) {
        ESP_LOGE(kTag, "Failed to allocate JSON array");
    }
    return array;
}

bool AddString(cJSON* parent, const char* key, const std::string& value)
{
    return parent != nullptr && cJSON_AddStringToObject(parent, key, value.c_str()) != nullptr;
}

bool AddString(cJSON* parent, const char* key, const char* value)
{
    return parent != nullptr && cJSON_AddStringToObject(parent, key, value) != nullptr;
}

bool AddNumber(cJSON* parent, const char* key, double value)
{
    return parent != nullptr && cJSON_AddNumberToObject(parent, key, value) != nullptr;
}

bool AddBool(cJSON* parent, const char* key, bool value)
{
    return parent != nullptr && cJSON_AddBoolToObject(parent, key, value) != nullptr;
}

std::string PrintJson(cJSON* root)
{
    char* raw = cJSON_PrintUnformatted(root);
    if (raw == nullptr) {
        ESP_LOGE(kTag, "Failed to serialize JSON");
        return "{}";
    }
    std::string result(raw);
    cJSON_free(raw);
    return result;
}

}  // namespace

Board::Board()
{
    Settings settings("board", true);
    uuid_ = settings.GetString("uuid");
    if (uuid_.empty()) {
        uuid_ = GenerateUuid();
        settings.SetString("uuid", uuid_);
    }
    ESP_LOGI(kTag, "UUID=%s SKU=%s", uuid_.c_str(), BOARD_NAME);
}

std::string Board::GenerateUuid()
{
    uint8_t random_bytes[16];
    esp_fill_random(random_bytes, sizeof(random_bytes));

    random_bytes[6] = (random_bytes[6] & 0x0f) | 0x40;
    random_bytes[8] = (random_bytes[8] & 0x3f) | 0x80;

    char uuid[37];
    snprintf(uuid, sizeof(uuid),
             "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
             random_bytes[0], random_bytes[1], random_bytes[2], random_bytes[3], random_bytes[4], random_bytes[5],
             random_bytes[6], random_bytes[7], random_bytes[8], random_bytes[9], random_bytes[10], random_bytes[11],
             random_bytes[12], random_bytes[13], random_bytes[14], random_bytes[15]);
    return uuid;
}

bool Board::GetBatteryLevel(int& level, bool& charging, bool& discharging)
{
    (void)level;
    (void)charging;
    (void)discharging;
    return false;
}

bool Board::GetTemperature(float& esp32_temperature)
{
    (void)esp32_temperature;
    return false;
}

Display* Board::GetDisplay()
{
    static NoDisplay display;
    return &display;
}

Camera* Board::GetCamera()
{
    return nullptr;
}

Led* Board::GetLed()
{
    static NullLed led;
    return &led;
}

std::string Board::GetSystemInfoJson()
{
    JsonPtr root = CreateObject();
    if (root == nullptr) {
        return "{}";
    }

    AddNumber(root.get(), "version", 2);
    AddString(root.get(), "language", Lang::CODE);
    AddNumber(root.get(), "flash_size", SystemInfo::GetFlashSize());
    AddNumber(root.get(), "minimum_free_heap_size", SystemInfo::GetMinimumFreeHeapSize());
    AddString(root.get(), "mac_address", SystemInfo::GetMacAddress());
    AddString(root.get(), "uuid", uuid_);
    AddString(root.get(), "chip_model_name", SystemInfo::GetChipModelName());

    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    JsonPtr chip = CreateObject();
    if (chip != nullptr) {
        AddNumber(chip.get(), "model", chip_info.model);
        AddNumber(chip.get(), "cores", chip_info.cores);
        AddNumber(chip.get(), "revision", chip_info.revision);
        AddNumber(chip.get(), "features", chip_info.features);
        cJSON_AddItemToObject(root.get(), "chip_info", chip.release());
    }

    const esp_app_desc_t* app_desc = esp_app_get_description();
    JsonPtr application = CreateObject();
    if (application != nullptr) {
        AddString(application.get(), "name", app_desc->project_name);
        AddString(application.get(), "version", app_desc->version);
        AddString(application.get(), "fork", firmware_identity::kFork);
        AddString(application.get(), "compile_time",
                  std::string(app_desc->date) + "T" + std::string(app_desc->time) + "Z");
        AddString(application.get(), "idf_version", app_desc->idf_ver);

        char sha256[65] = {};
        for (size_t i = 0; i < sizeof(app_desc->app_elf_sha256); ++i) {
            snprintf(sha256 + (i * 2), sizeof(sha256) - (i * 2), "%02x", app_desc->app_elf_sha256[i]);
        }
        AddString(application.get(), "elf_sha256", sha256);
        cJSON_AddItemToObject(root.get(), "application", application.release());
    }

    JsonPtr partitions = CreateArray();
    if (partitions != nullptr) {
        esp_partition_iterator_t iterator = esp_partition_find(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, nullptr);
        while (iterator != nullptr) {
            const esp_partition_t* partition = esp_partition_get(iterator);
            JsonPtr entry = CreateObject();
            if (entry != nullptr) {
                AddString(entry.get(), "label", partition->label);
                AddNumber(entry.get(), "type", partition->type);
                AddNumber(entry.get(), "subtype", partition->subtype);
                AddNumber(entry.get(), "address", partition->address);
                AddNumber(entry.get(), "size", partition->size);
                cJSON_AddItemToArray(partitions.get(), entry.release());
            }
            iterator = esp_partition_next(iterator);
        }
        cJSON_AddItemToObject(root.get(), "partition_table", partitions.release());
    }

    const esp_partition_t* running_partition = esp_ota_get_running_partition();
    JsonPtr ota = CreateObject();
    if (ota != nullptr && running_partition != nullptr) {
        AddString(ota.get(), "label", running_partition->label);
        cJSON_AddItemToObject(root.get(), "ota", ota.release());
    }

    if (Display* display = GetDisplay(); display != nullptr) {
        JsonPtr display_json = CreateObject();
        if (display_json != nullptr) {
            const bool likely_monochrome = display->width() > 0 && display->height() > 0 && display->height() <= 64;
            AddBool(display_json.get(), "monochrome", likely_monochrome);
            AddNumber(display_json.get(), "width", display->width());
            AddNumber(display_json.get(), "height", display->height());
            cJSON_AddItemToObject(root.get(), "display", display_json.release());
        }
    }

    JsonPtr board_json(cJSON_Parse(GetBoardJson().c_str()));
    if (board_json != nullptr) {
        cJSON_AddItemToObject(root.get(), "board", board_json.release());
    }

    return PrintJson(root.get());
}
