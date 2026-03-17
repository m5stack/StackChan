/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
// https://github.com/espressif/esp-idf/blob/v5.5.2/examples/system/ota/simple_ota_example/main/simple_ota_example.c

// #define CONFIG_EXAMPLE_USE_CERT_BUNDLE 1

#include "ota.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "string.h"
#ifdef CONFIG_EXAMPLE_USE_CERT_BUNDLE
#include "esp_crt_bundle.h"
#endif

static const char *TAG = "ota";

#define OTA_URL_SIZE 256

const char *server_cert_pem_start =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIC+jCCAeKgAwIBAgIUBhxIdyxfbSgLqwvNPcelYLE88Y0wDQYJKoZIhvcNAQEL\n"
    "BQAwJDEiMCAGA1UEAwwZU3RhY2tjaGFuIE9UQSBUZXN0IFNlcnZlcjAeFw0yNjAx\n"
    "MjAwMjE4NTRaFw0yNzAxMjAwMjE4NTRaMCQxIjAgBgNVBAMMGVN0YWNrY2hhbiBP\n"
    "VEEgVGVzdCBTZXJ2ZXIwggEiMA0GCSqGSIb3DQEBAQUAA4IBDwAwggEKAoIBAQCg\n"
    "xRAxiju6admmBoCeC9wPYLLE9tUHccYM69O637Qapw3n8zH9LURBTJ7ivCeVQC8T\n"
    "+pZGLkS4NsExS+oHnyyr+OTB3ykKxOoOb7Sk0izxy0+gDEEojhNUPYc/mNgAq4yw\n"
    "ELmq5ymFMe1nFbm++menfdsYcyFNw05J/8c0gaOM2mj+GbGrzLUXVyAZg3JNKFEQ\n"
    "SfIgI41XNAWWNozRSrtbPUSBbmuCaOoQeNrU1jt5mBsVuHk5p7wIt2jJUe13a6UE\n"
    "0N179S+1Cn0fMEceJWYBS5FBSU83L2DMJi+FXI/877NKq/gifzYccG8tg1mYabba\n"
    "lKGNdhtxx6UJv0DtobUlAgMBAAGjJDAiMCAGA1UdEQQZMBeCCWxvY2FsaG9zdIcE\n"
    "fwAAAYcEwKgUFzANBgkqhkiG9w0BAQsFAAOCAQEAoHi9RYFuB6EKVU21rWSDPf/O\n"
    "9PhDcp8+hrE5OdgowhTeZDLwy6b/uAF7Vgo/Ojk/oqrHvFJlHEH3wQFTbWjUIJ3P\n"
    "aAzHrZEYMOGRTPdELiilke6+HbMMbOGfhFqt7es8eXPwFdzraPGaodwf0W8/AYSk\n"
    "QQoW+5zbkOS5p1teQUTsnrccjfe1xDx/mz1bW1cHK69pQNGbVtWpGs5IDMesDcqL\n"
    "nFnhrKtz3LFETQH5ItSueFUEurnyqxY+4rV2kx8emGtzPSokwbGmIpVe+Bei7sSo\n"
    "fQAFvk9rhUf1vvphC+Hci1uR60yDMq1P7S9JT+rRPTqdi+RoMK3LpVx67Ie0Hg==\n"
    "-----END CERTIFICATE-----\n"
    "";

static esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_DISCONNECTED");
            break;
        case HTTP_EVENT_REDIRECT:
            ESP_LOGD(TAG, "HTTP_EVENT_REDIRECT");
            break;
    }
    return ESP_OK;
}

esp_err_t my_esp_https_ota(const esp_https_ota_config_t *ota_config, void (*on_progress)(int progress))
{
    if (ota_config == NULL || ota_config->http_config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_https_ota_handle_t https_ota_handle = NULL;
    esp_err_t err                           = esp_https_ota_begin(ota_config, &https_ota_handle);
    if (err != ESP_OK) {
        return err;
    }

    int last_progress = -1;
    while (1) {
        err = esp_https_ota_perform(https_ota_handle);
        if (err != ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
            break;
        }

        // --- 进度计算逻辑 ---
        int total_size = esp_https_ota_get_image_size(https_ota_handle);
        int read_size  = esp_https_ota_get_image_len_read(https_ota_handle);

        if (total_size > 0) {
            int progress = (read_size * 100) / total_size;

            if (progress != last_progress) {
                last_progress = progress;
                if (on_progress) {
                    on_progress(progress);
                }
            }
        }
    }

    if (err != ESP_OK) {
        esp_https_ota_abort(https_ota_handle);
        return err;
    }

    esp_err_t ota_finish_err = esp_https_ota_finish(https_ota_handle);
    if (ota_finish_err != ESP_OK) {
        return ota_finish_err;
    }
    return ESP_OK;
}

void start_ota_update(const char *url, void (*on_progress)(int progress))
{
    ESP_LOGI(TAG, "Starting OTA example task");
#ifdef CONFIG_EXAMPLE_FIRMWARE_UPGRADE_BIND_IF
    esp_netif_t *netif = get_example_netif_from_desc(bind_interface_name);
    if (netif == NULL) {
        ESP_LOGE(TAG, "Can't find netif from interface description");
        abort();
    }
    struct ifreq ifr;
    esp_netif_get_netif_impl_name(netif, ifr.ifr_name);
    ESP_LOGI(TAG, "Bind interface name is %s", ifr.ifr_name);
#endif
    esp_http_client_config_t config = {
        .url = url,
#ifdef CONFIG_EXAMPLE_USE_CERT_BUNDLE
        .crt_bundle_attach = esp_crt_bundle_attach,
#else
        .cert_pem = (char *)server_cert_pem_start,
#endif /* CONFIG_EXAMPLE_USE_CERT_BUNDLE */
        .event_handler     = _http_event_handler,
        .keep_alive_enable = true,
#ifdef CONFIG_EXAMPLE_FIRMWARE_UPGRADE_BIND_IF
        .if_name = &ifr,
#endif
#if CONFIG_EXAMPLE_TLS_DYN_BUF_RX_STATIC
        /* This part applies static buffer strategy for rx dynamic buffer.
         * This is to avoid frequent allocation and deallocation of dynamic buffer.
         */
        .tls_dyn_buf_strategy = HTTP_TLS_DYN_BUF_RX_STATIC,
#endif /* CONFIG_EXAMPLE_TLS_DYN_BUF_RX_STATIC */
    };

#ifdef CONFIG_EXAMPLE_FIRMWARE_UPGRADE_URL_FROM_STDIN
    char url_buf[OTA_URL_SIZE];
    if (strcmp(config.url, "FROM_STDIN") == 0) {
        example_configure_stdin_stdout();
        fgets(url_buf, OTA_URL_SIZE, stdin);
        int len          = strlen(url_buf);
        url_buf[len - 1] = '\0';
        config.url       = url_buf;
    } else {
        ESP_LOGE(TAG, "Configuration mismatch: wrong firmware upgrade image url");
        abort();
    }
#endif

#ifdef CONFIG_EXAMPLE_SKIP_COMMON_NAME_CHECK
    config.skip_cert_common_name_check = true;
#endif

    esp_https_ota_config_t ota_config = {
        .http_config = &config,
    };
    ESP_LOGI(TAG, "Attempting to download update from %s", config.url);
    esp_err_t ret = my_esp_https_ota(&ota_config, on_progress);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "OTA Succeed, Rebooting...");
        esp_restart();
    } else {
        ESP_LOGE(TAG, "Firmware upgrade failed");
    }
}
