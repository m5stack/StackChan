#include "system_reset.h"

#include <driver/gpio.h>
#include <esp_log.h>
#include <esp_partition.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <nvs_flash.h>

namespace {
constexpr char kTag[] = "SystemReset";
}

SystemReset::SystemReset(gpio_num_t reset_nvs_pin, gpio_num_t reset_factory_pin)
    : reset_nvs_pin_(reset_nvs_pin)
    , reset_factory_pin_(reset_factory_pin)
{
    const gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << reset_nvs_pin_) | (1ULL << reset_factory_pin_),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
}

void SystemReset::CheckButtons()
{
    if (gpio_get_level(reset_factory_pin_) == 0) {
        ESP_LOGI(kTag, "Factory reset button pressed");
        ResetNvsFlash();
        ResetToFactory();
    }

    if (gpio_get_level(reset_nvs_pin_) == 0) {
        ESP_LOGI(kTag, "NVS reset button pressed");
        ResetNvsFlash();
    }
}

void SystemReset::ResetNvsFlash()
{
    ESP_LOGI(kTag, "Resetting NVS flash");

    esp_err_t err = nvs_flash_erase();
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "Failed to erase NVS flash");
    }

    err = nvs_flash_init();
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "Failed to initialize NVS flash");
    }
}

void SystemReset::ResetToFactory()
{
    ESP_LOGI(kTag, "Resetting to factory");

    const esp_partition_t* partition =
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_OTA, nullptr);
    if (partition == nullptr) {
        ESP_LOGE(kTag, "Failed to find otadata partition");
        return;
    }

    esp_partition_erase_range(partition, 0, partition->size);
    ESP_LOGI(kTag, "Erased otadata partition");

    RestartInSeconds(3);
}

void SystemReset::RestartInSeconds(int seconds)
{
    for (int remaining = seconds; remaining > 0; --remaining) {
        ESP_LOGI(kTag, "Resetting in %d seconds", remaining);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    esp_restart();
}
