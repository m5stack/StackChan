#include "system_info.h"

#include <esp_app_desc.h>
#include <esp_flash.h>
#include <esp_log.h>
#include <esp_mac.h>
#include <esp_partition.h>
#include <esp_pm.h>
#include <esp_system.h>
#include <esp_heap_caps.h>
#include <freertos/task.h>

#if CONFIG_IDF_TARGET_ESP32P4
#include "esp_wifi_remote.h"
#endif

namespace {
constexpr const char* kTag = "SystemInfo";
}

size_t SystemInfo::GetFlashSize()
{
    uint32_t flash_size = 0;
    if (esp_flash_get_size(nullptr, &flash_size) != ESP_OK) {
        ESP_LOGE(kTag, "Failed to get flash size");
        return 0;
    }
    return static_cast<size_t>(flash_size);
}

size_t SystemInfo::GetMinimumFreeHeapSize()
{
    return esp_get_minimum_free_heap_size();
}

size_t SystemInfo::GetFreeHeapSize()
{
    return esp_get_free_heap_size();
}

std::string SystemInfo::GetMacAddress()
{
    uint8_t mac[6] = {};
#if CONFIG_IDF_TARGET_ESP32P4
    esp_wifi_get_mac(WIFI_IF_STA, mac);
#else
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
#endif
    char mac_str[18] = {};
    snprintf(mac_str, sizeof(mac_str), "%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2], mac[3], mac[4],
             mac[5]);
    return std::string(mac_str);
}

std::string SystemInfo::GetChipModelName()
{
    return std::string(CONFIG_IDF_TARGET);
}

std::string SystemInfo::GetUserAgent()
{
    const auto* app_desc = esp_app_get_description();
    return std::string(BOARD_NAME "/") + app_desc->version;
}

esp_err_t SystemInfo::PrintTaskCpuUsage(TickType_t xTicksToWait)
{
    constexpr UBaseType_t kArraySizeOffset = 5;
    TaskStatus_t* start_array              = nullptr;
    TaskStatus_t* end_array                = nullptr;
    UBaseType_t start_array_size           = uxTaskGetNumberOfTasks() + kArraySizeOffset;
    UBaseType_t end_array_size             = 0;
    configRUN_TIME_COUNTER_TYPE start_run_time = 0;
    configRUN_TIME_COUNTER_TYPE end_run_time   = 0;
    uint32_t total_elapsed_time                = 0;
    esp_err_t result                           = ESP_OK;

    start_array = static_cast<TaskStatus_t*>(malloc(sizeof(TaskStatus_t) * start_array_size));
    if (start_array == nullptr) {
        return ESP_ERR_NO_MEM;
    }

    start_array_size = uxTaskGetSystemState(start_array, start_array_size, &start_run_time);
    if (start_array_size == 0) {
        result = ESP_ERR_INVALID_SIZE;
        goto cleanup;
    }

    vTaskDelay(xTicksToWait);

    end_array_size = uxTaskGetNumberOfTasks() + kArraySizeOffset;
    end_array = static_cast<TaskStatus_t*>(malloc(sizeof(TaskStatus_t) * end_array_size));
    if (end_array == nullptr) {
        result = ESP_ERR_NO_MEM;
        goto cleanup;
    }

    end_array_size = uxTaskGetSystemState(end_array, end_array_size, &end_run_time);
    if (end_array_size == 0) {
        result = ESP_ERR_INVALID_SIZE;
        goto cleanup;
    }

    total_elapsed_time = end_run_time - start_run_time;
    if (total_elapsed_time == 0) {
        result = ESP_ERR_INVALID_STATE;
        goto cleanup;
    }

    printf("| Task | Run Time | Percentage\n");
    for (UBaseType_t i = 0; i < start_array_size; ++i) {
        int matched_index = -1;
        for (UBaseType_t j = 0; j < end_array_size; ++j) {
            if (start_array[i].xHandle == end_array[j].xHandle) {
                matched_index = static_cast<int>(j);
                start_array[i].xHandle = nullptr;
                end_array[j].xHandle = nullptr;
                break;
            }
        }

        if (matched_index >= 0) {
            const uint32_t task_elapsed_time =
                end_array[matched_index].ulRunTimeCounter - start_array[i].ulRunTimeCounter;
            const uint32_t percentage_time =
                (task_elapsed_time * 100UL) / (total_elapsed_time * CONFIG_FREERTOS_NUMBER_OF_CORES);
            printf("| %-16s | %8lu | %4lu%%\n", start_array[i].pcTaskName, task_elapsed_time, percentage_time);
        }
    }

    for (UBaseType_t i = 0; i < start_array_size; ++i) {
        if (start_array[i].xHandle != nullptr) {
            printf("| %s | Deleted\n", start_array[i].pcTaskName);
        }
    }
    for (UBaseType_t i = 0; i < end_array_size; ++i) {
        if (end_array[i].xHandle != nullptr) {
            printf("| %s | Created\n", end_array[i].pcTaskName);
        }
    }

cleanup:
    free(start_array);
    free(end_array);
    return result;
}

void SystemInfo::PrintTaskList()
{
    char buffer[1000] = {};
    vTaskList(buffer);
    ESP_LOGI(kTag, "Task list: \n%s", buffer);
}

void SystemInfo::PrintHeapStats()
{
    const int free_sram = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    const int min_free_sram = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
    ESP_LOGI(kTag, "free sram: %u minimal sram: %u", free_sram, min_free_sram);
}

void SystemInfo::PrintPmLocks()
{
    esp_pm_dump_locks(stdout);
}
