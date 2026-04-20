/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal.h"
#include "utils/ota/ota.h"
#include <mooncake_log.h>
#include <memory>
#include <board.h>
#include <cJSON.h>

static const std::string_view _tag = "HAL-AppCenter";

static const std::string_view _app_info_list_url = "http://47.113.125.164:12800/stackChan/apps";

static const char *get_json_string(cJSON *item, std::initializer_list<const char *> keys)
{
    for (const auto *key : keys) {
        cJSON *value = cJSON_GetObjectItemCaseSensitive(item, key);
        if (cJSON_IsString(value) && value->valuestring != nullptr) {
            return value->valuestring;
        }
    }

    return nullptr;
}

app_center::AppInfoList_t Hal::fetchAppList()
{
    app_center::AppInfoList_t app_list;
    auto &board  = Board::GetInstance();
    auto network = board.GetNetwork();

    auto http = network->CreateHttp(0);
    if (!http->Open("GET", std::string(_app_info_list_url))) {
        mclog::tagError(_tag, "failed to open http connection");
        return app_list;
    }

    if (http->GetStatusCode() != 200) {
        mclog::tagError(_tag, "failed to fetch app list, status code: {}", http->GetStatusCode());
        http->Close();
        return app_list;
    }

    std::string data = http->ReadAll();
    http->Close();

    cJSON *root = cJSON_Parse(data.c_str());
    if (root == NULL) {
        mclog::tagError(_tag, "failed to parse json response");
        return app_list;
    }

    cJSON *app_array = nullptr;
    if (cJSON_IsArray(root)) {
        app_array = root;
    } else if (cJSON_IsObject(root)) {
        cJSON *code = cJSON_GetObjectItemCaseSensitive(root, "code");
        if (cJSON_IsNumber(code) && code->valueint != 0) {
            cJSON *message = cJSON_GetObjectItemCaseSensitive(root, "message");
            mclog::tagError(_tag, "failed to fetch app list, code: {}, message: {}", code->valueint,
                            cJSON_IsString(message) ? message->valuestring : "unknown");
            cJSON_Delete(root);
            return app_list;
        }

        app_array = cJSON_GetObjectItemCaseSensitive(root, "data");
        if (!cJSON_IsArray(app_array)) {
            mclog::tagError(_tag, "invalid app list response: data is not an array");
            cJSON_Delete(root);
            return app_list;
        }
    }

    if (cJSON_IsArray(app_array)) {
        cJSON *item = NULL;
        cJSON_ArrayForEach(item, app_array)
        {
            app_center::AppInfo_t info;

            if (const char *name = get_json_string(item, {"appName", "name"})) {
                info.name = name;
            }

            if (const char *icon = get_json_string(item, {"iconUrl", "appIcon", "icon"})) {
                info.iconUrl = icon;
            }

            if (const char *desc = get_json_string(item, {"description", "appDescription", "desc"})) {
                info.description = desc;
            }

            if (const char *url = get_json_string(item, {"firmwareUrl", "downloadUrl", "otaUrl", "url"})) {
                info.firmwareUrl = url;
            }

            app_list.push_back(info);
        }
    } else {
        mclog::tagError(_tag, "invalid app list response: unexpected json shape");
    }

    cJSON_Delete(root);
    return app_list;
}

static std::function<void(int)> _on_progress = nullptr;
static void ota_callback(int progress)
{
    if (_on_progress) {
        _on_progress(progress);
    }
}

void Hal::launchApp(std::string_view url, std::function<void(int)> onProgress)
{
    mclog::tagInfo(_tag, "launching app from url: {}", url);
    _on_progress = onProgress;
    start_ota_update(url.data(), ota_callback);
}
