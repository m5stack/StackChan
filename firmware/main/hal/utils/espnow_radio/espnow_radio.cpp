#include "espnow_radio.h"
#include <algorithm>
#include <esp_event.h>
#include <esp_netif.h>
#include <esp_wifi.h>
#include <wifi_manager.h>

esp_err_t EspNowRadio::Start(int channel)
{
    channel = std::clamp(channel, 1, 13);
    if (started_) return esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);

    esp_err_t error = esp_netif_init();
    if (error != ESP_OK && error != ESP_ERR_INVALID_STATE) return error;
    error = esp_event_loop_create_default();
    if (error != ESP_OK && error != ESP_ERR_INVALID_STATE) return error;

    // Stop the manager's timers and event subscriptions before taking the radio.
    // Merely disconnecting the driver lets its scan/reconnect logic change channel.
    auto& wifi = WifiManager::GetInstance();
    if (wifi.IsInitialized()) {
        // Stopping config AP emits ConfigModeExit, whose board callback starts
        // another connection attempt. Leave that mode intact and report busy.
        if (wifi.IsConfigMode()) return ESP_ERR_INVALID_STATE;
        wifi.StopStation();
    }

    wifi_mode_t mode;
    error = esp_wifi_get_mode(&mode);
    if (error == ESP_ERR_WIFI_NOT_INIT) {
        wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
        config.nvs_enable         = false;
        error                     = esp_wifi_init(&config);
        if (error != ESP_OK) return error;
        owns_driver_ = true;
    } else if (error != ESP_OK) {
        return error;
    }

    // A borrowed driver may still be running even without an active manager.
    // Do not destroy any existing netif or erase persistent Wi-Fi settings.
    if ((error = esp_wifi_stop()) == ESP_OK && (error = esp_wifi_set_storage(WIFI_STORAGE_RAM)) == ESP_OK &&
        (error = esp_wifi_set_mode(WIFI_MODE_STA)) == ESP_OK && (error = esp_wifi_start()) == ESP_OK) {
        started_ = true;
        if ((error = esp_wifi_set_ps(WIFI_PS_NONE)) == ESP_OK &&
            (error = esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE)) == ESP_OK) {
            return ESP_OK;
        }
    }
    Stop();
    return error;
}

void EspNowRadio::Stop()
{
    if (started_) esp_wifi_stop();
    if (owns_driver_) esp_wifi_deinit();
    started_     = false;
    owns_driver_ = false;
}
