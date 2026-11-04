/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal.h"
#include "utils/espnow_radio/espnow_radio.h"
#include <mooncake_log.h>
#include <esp_log.h>
#include <esp_mac.h>
#include <espnow.h>
#include <espnow_storage.h>
#include <espnow_utils.h>
#include <esp_check.h>

static const std::string_view _tag = "HAL-EspNow";

static EspNowRadio s_radio;
static bool s_espnow_ready = false;

static esp_err_t _handle_espnow_received(uint8_t* src_addr, void* data, size_t size, wifi_pkt_rx_ctrl_t* rx_ctrl)
{
    const char* TAG = "EspNow";

    ESP_PARAM_CHECK(src_addr);
    ESP_PARAM_CHECK(data);
    ESP_PARAM_CHECK(size);
    ESP_PARAM_CHECK(rx_ctrl);

    static uint32_t count = 0;

    ESP_LOGI(TAG, "espnow_recv, <%" PRIu32 "> [" MACSTR "][%d][%d][%u]: %.*s", count++, MAC2STR(src_addr),
             rx_ctrl->channel, rx_ctrl->rssi, size, size, "~");

    std::vector<uint8_t> received_data((uint8_t*)data, (uint8_t*)data + size);
    GetHAL().onEspNowData.emit(received_data);

    return ESP_OK;
}

bool Hal::startEspNow(int channel)
{
    mclog::tagInfo(_tag, "start EspNow on channel {}", channel);

    esp_err_t error = s_radio.Start(channel);
    if (error != ESP_OK) {
        mclog::tagError(_tag, "radio startup failed: {}", esp_err_to_name(error));
        return false;
    }
    if (s_espnow_ready) return true;

    espnow_config_t espnow_config = ESPNOW_INIT_CONFIG_DEFAULT();

    // 2. 修改关键参数以兼容 Arduino
    espnow_config.forward_enable         = false;  // 关闭转发（多跳），Arduino 无法解析带转发头的包
    espnow_config.forward_switch_channel = false;  // 关闭自动切信道
    espnow_config.send_retry_num         = 5;      // 失败重试次数（可按需调，建议5-10）

    // 3. 修改接收使能开关
    espnow_config.receive_enable.forward = false;  // 关闭转发包接收
    espnow_config.receive_enable.data    = true;   // 必须开启这个，才能接收 Arduino 发来的普通数据包

    error = espnow_init(&espnow_config);
    if (error != ESP_OK) {
        mclog::tagError(_tag, "ESP-NOW init failed: {}", esp_err_to_name(error));
        s_radio.Stop();
        return false;
    }
    error = espnow_set_config_for_data_type(ESPNOW_DATA_TYPE_DATA, true, _handle_espnow_received);
    if (error != ESP_OK) {
        mclog::tagError(_tag, "receive setup failed: {}", esp_err_to_name(error));
        espnow_deinit();
        s_radio.Stop();
        return false;
    }
    s_espnow_ready = true;
    return true;
}

bool Hal::espNowSend(const std::vector<uint8_t>& data, const uint8_t* destAddr)
{
    if (!s_espnow_ready) return false;
    mclog::tagInfo(_tag, "send data with size: {}", data.size());

    espnow_frame_head_t frame_head = ESPNOW_FRAME_CONFIG_DEFAULT();
    esp_err_t ret                  = ESP_FAIL;

    if (destAddr == nullptr) {
        ret = espnow_send(ESPNOW_DATA_TYPE_DATA, ESPNOW_ADDR_BROADCAST, data.data(), data.size(), &frame_head,
                          portMAX_DELAY);
    } else {
        ret = espnow_send(ESPNOW_DATA_TYPE_DATA, destAddr, data.data(), data.size(), &frame_head, portMAX_DELAY);
    }

    if (ret != ESP_OK) {
        mclog::tagError(_tag, "send failed: {}", esp_err_to_name(ret));
        return false;
    }
    return true;
}

#include <driver/gpio.h>

void Hal::setLaserEnabled(bool enabled)
{
    static bool laser_enabled = false;
    static bool is_inited     = false;

    if (laser_enabled == enabled) {
        return;
    }

    const gpio_num_t laser_pin = GPIO_NUM_2;

    if (!is_inited) {
        gpio_reset_pin(laser_pin);
        gpio_set_direction(laser_pin, GPIO_MODE_OUTPUT);
        gpio_set_pull_mode(laser_pin, GPIO_PULLUP_ONLY);
        is_inited = true;
    }

    mclog::tagInfo(_tag, "set laser {}", enabled ? "enabled" : "disabled");

    if (enabled) {
        gpio_set_level(laser_pin, 1);
    } else {
        gpio_set_level(laser_pin, 0);
    }
    laser_enabled = enabled;
}
