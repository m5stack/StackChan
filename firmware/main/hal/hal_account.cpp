/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal.h"
#include "utils/secret_logic/secret_logic.h"
#include <settings.h>
#include <mooncake_log.h>
#include <memory>
#include <board.h>
#include <cJSON.h>

static const std::string_view _tag = "HAL-Account";

static const std::string_view _setting_ns              = "account";
static const std::string_view _setting_username_key    = "username";
static const std::string_view _setting_device_name_key = "device_name";

static const std::string_view _get_user_account_info_url    = "http://47.113.125.164:12800/stackChan/device/user";
static const std::string_view _get_device_info_url          = "http://47.113.125.164:12800/stackChan/device/info";
static const std::string_view _unbind_user_account_info_url = "http://47.113.125.164:12800/stackChan/device/unbind";

static bool request_authorized_get(std::string_view token, std::string_view url, std::string &response,
                                   std::function<void(std::string_view)> onLog)
{
    auto &board  = Board::GetInstance();
    auto network = board.GetNetwork();
    auto http    = network->CreateHttp(0);

    if (!http) {
        mclog::tagError(_tag, "failed to create http client");
        onLog("Failed to create HTTP client");
        return false;
    }

    http->SetHeader("Authorization", std::string(token));
    if (!http->Open("GET", std::string(url))) {
        mclog::tagError(_tag, "failed to open http request: {}", url);
        onLog("Failed to connect to server");
        return false;
    }

    int status_code = http->GetStatusCode();
    if (status_code != 200) {
        mclog::tagError(_tag, "http get failed, status code: {}, url: {}", status_code, url);
        onLog("HTTP Request Failed");
        return false;
    }

    response = http->ReadAll();
    mclog::tagInfo(_tag, "response from {}: {}", url, response);
    return true;
}

static bool fetch_username(std::string_view token, std::string &username, std::function<void(std::string_view)> onLog)
{
    onLog("Updating user account info...");

    std::string response;
    if (!request_authorized_get(token, _get_user_account_info_url, response, onLog)) {
        return false;
    }

    cJSON *root = cJSON_Parse(response.c_str());
    if (!root) {
        mclog::tagError(_tag, "failed to parse user account json");
        onLog("Failed to parse response");
        return false;
    }

    bool success = false;
    cJSON *code  = cJSON_GetObjectItem(root, "code");
    cJSON *data  = cJSON_GetObjectItem(root, "data");

    const char *username_value = nullptr;
    if (data && cJSON_IsString(data)) {
        username_value = data->valuestring;
    } else if (data && cJSON_IsObject(data)) {
        cJSON *username_json = cJSON_GetObjectItem(data, "username");
        if (username_json && cJSON_IsString(username_json)) {
            username_value = username_json->valuestring;
        }
    }

    if (code && cJSON_IsNumber(code) && code->valueint == 0 && username_value != nullptr) {
        username = username_value;
        success  = true;
    } else {
        mclog::tagError(_tag, "invalid user account response format or error code");
        onLog("Invalid response from server");
    }

    cJSON_Delete(root);
    return success;
}

static bool fetch_device_name(std::string_view token, std::string &deviceName,
                              std::function<void(std::string_view)> onLog)
{
    onLog("Updating device info...");

    std::string response;
    if (!request_authorized_get(token, _get_device_info_url, response, onLog)) {
        return false;
    }

    cJSON *root = cJSON_Parse(response.c_str());
    if (!root) {
        mclog::tagError(_tag, "failed to parse device info json");
        onLog("Failed to parse response");
        return false;
    }

    bool success  = false;
    cJSON *code   = cJSON_GetObjectItem(root, "code");
    cJSON *data   = cJSON_GetObjectItem(root, "data");
    cJSON *target = root;

    if (code && cJSON_IsNumber(code)) {
        if (code->valueint != 0) {
            mclog::tagError(_tag, "device info request failed, code: {}", code->valueint);
            onLog("Invalid response from server");
            cJSON_Delete(root);
            return false;
        }
        if (data && cJSON_IsObject(data)) {
            target = data;
        }
    }

    cJSON *name_json = cJSON_GetObjectItem(target, "name");
    if (name_json && cJSON_IsString(name_json)) {
        deviceName = name_json->valuestring;
        success    = true;
    } else {
        mclog::tagError(_tag, "invalid device info response format");
        onLog("Invalid response from server");
    }

    cJSON_Delete(root);
    return success;
}

UserAccountInfo_t Hal::getUserAccountInfo()
{
    UserAccountInfo_t info;
    Settings settings(_setting_ns.data(), false);
    info.username   = settings.GetString(_setting_username_key.data(), "Account Info");
    info.deviceName = settings.GetString(_setting_device_name_key.data(), "");
    return info;
}

bool Hal::updateAccountInfo(std::function<void(std::string_view)> onLog)
{
    std::string token = secret_logic::generate_auth_token();

    std::string username;
    if (!fetch_username(token, username, onLog)) {
        return false;
    }

    std::string device_name;
    if (!fetch_device_name(token, device_name, onLog)) {
        return false;
    }

    Settings settings(_setting_ns.data(), true);
    settings.SetString(_setting_username_key.data(), username);
    settings.SetString(_setting_device_name_key.data(), device_name);

    mclog::tagInfo(_tag, "account updated: username={}, device_name={}", username, device_name);
    onLog(std::string("Account updated: ") + username);
    return true;
}

bool Hal::unbindAccount(std::function<void(std::string_view)> onLog)
{
    std::string token = secret_logic::generate_auth_token();

    auto &board  = Board::GetInstance();
    auto network = board.GetNetwork();
    auto http    = network->CreateHttp(0);

    if (!http) {
        mclog::tagError(_tag, "failed to create http client");
        onLog("Failed to create HTTP client");
        return false;
    }

    http->SetHeader("Authorization", token);
    http->SetContent(std::string());
    mclog::tagInfo(_tag, "requesting to unbind account...");
    onLog("Unbinding account...");

    if (!http->Open("POST", std::string(_unbind_user_account_info_url))) {
        mclog::tagError(_tag, "failed to open http request");
        onLog("Failed to connect to server");
        return false;
    }

    int status_code = http->GetStatusCode();
    if (status_code != 200) {
        mclog::tagError(_tag, "http post failed, status code: {}", status_code);
        onLog("HTTP Request Failed");
        return false;
    }

    std::string response = http->ReadAll();
    mclog::tagInfo(_tag, "response: {}", response);

    cJSON *root = cJSON_Parse(response.c_str());
    if (!root) {
        mclog::tagError(_tag, "failed to parse json");
        onLog("Failed to parse response");
        return false;
    }

    bool success = false;
    cJSON *code  = cJSON_GetObjectItem(root, "code");

    if (code && cJSON_IsNumber(code) && code->valueint == 0) {
        Settings settings(_setting_ns.data(), true);
        settings.SetString(_setting_username_key.data(), "Account Info");
        settings.SetString(_setting_device_name_key.data(), "");
        mclog::tagInfo(_tag, "account unbound successfully");
        onLog("Account unbound successfully");
        success = true;
    } else {
        mclog::tagError(_tag, "invalid response format or error code");
        cJSON *msg = cJSON_GetObjectItem(root, "message");
        if (msg && cJSON_IsString(msg)) {
            onLog(std::string("Unbind failed: ") + msg->valuestring);
        } else {
            onLog("Invalid response from server");
        }
    }

    cJSON_Delete(root);

    if (success) {
        resetAppConfiged();
    }

    return success;
}
