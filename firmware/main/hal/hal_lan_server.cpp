/*
 * SPDX-License-Identifier: MIT
 *
 * One HTTP server on the LAN for reefwatch frames and the MCP endpoint, plus an mDNS name
 * (stackchan.local) and an _mcp._tcp advertisement so ESP-Claw can discover the robot.
 */
#include "hal.h"
#include "hal_lan_server.h"
#include <mooncake_log.h>
#include <wifi_manager.h>
#include <mdns.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static const std::string_view _tag = "HAL-LAN";

static void start_mdns(uint16_t port)
{
    if (mdns_init() != ESP_OK) {
        mclog::tagError(_tag, "mdns init failed");
        return;
    }
    mdns_hostname_set(CONFIG_STACKCHAN_LAN_HOSTNAME);
    mdns_instance_name_set("StackChan");
    mdns_service_add(nullptr, "_http", "_tcp", port, nullptr, 0);

#if CONFIG_STACKCHAN_LAN_MCP_ENABLE
    mdns_txt_item_t txt[] = {{"endpoint", CONFIG_STACKCHAN_LAN_MCP_ENDPOINT}};
    mdns_service_add(nullptr, "_mcp", "_tcp", port, txt, sizeof(txt) / sizeof(txt[0]));
#endif
}

static void start_server_task(void* param)
{
    // The server binds to any address, but waiting for Wi-Fi gives a useful log line and lets mDNS start
    while (!WifiManager::GetInstance().IsConnected()) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    httpd_config_t config   = HTTPD_DEFAULT_CONFIG();
    config.server_port      = CONFIG_STACKCHAN_LAN_SERVER_PORT;
    config.ctrl_port        = ESP_HTTPD_DEF_CTRL_PORT + 7;  // stay clear of the Wi-Fi config portal's server
    config.stack_size       = 8192;                         // JPEG decoding and MCP tool calls run on this task
    config.task_caps        = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;  // internal SRAM is tight; no handler writes flash
    config.max_open_sockets = 4;
    config.lru_purge_enable = true;

    httpd_handle_t server = nullptr;
    if (httpd_start(&server, &config) != ESP_OK) {
        mclog::tagError(_tag, "failed to start http server on port {}", config.server_port);
        vTaskDelete(nullptr);
        return;
    }

#if CONFIG_STACKCHAN_REEFWATCH_ENABLE
    reefwatch_register_uris(server);
#endif
#if CONFIG_STACKCHAN_LAN_MCP_ENABLE
    lan_mcp_register_uris(server);
#endif
    start_mdns(config.server_port);

    mclog::tagInfo(_tag, "listening on http://{}:{} (http://{}.local)", WifiManager::GetInstance().GetIpAddress(),
                   config.server_port, CONFIG_STACKCHAN_LAN_HOSTNAME);
    vTaskDelete(nullptr);
}

void Hal::startLanServer()
{
#if CONFIG_STACKCHAN_REEFWATCH_ENABLE || CONFIG_STACKCHAN_LAN_MCP_ENABLE
    xTaskCreate(start_server_task, "lan_server", 4096, nullptr, 3, nullptr);
#endif
}
