#include "local_control_websocket_server.h"

#include "config.h"
#include "hal_bridge.h"
#include "mcp_server.h"
#include "../hal.h"

#include <cJSON.h>
#include <esp_log.h>
#include <esp_mac.h>
#include <esp_random.h>
#include <esp_timer.h>
#include <mbedtls/md.h>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <new>
#include <string>
#include <utility>

#ifndef STACKCHAN_CONTROL_WS_TOKEN
#define STACKCHAN_CONTROL_WS_TOKEN "stackchan-local-dev"
#endif

#ifndef STACKCHAN_CONTROL_WS_MAX_MESSAGE_BYTES
#define STACKCHAN_CONTROL_WS_MAX_MESSAGE_BYTES 4096
#endif

static const char* TAG = "LocalControlWS";

static constexpr int64_t AUTH_CHALLENGE_TTL_US = 30 * 1000 * 1000;

static std::string BytesToHex(const uint8_t* bytes, size_t len)
{
    static constexpr char hex[] = "0123456789abcdef";
    std::string out;
    out.reserve(len * 2);
    for (size_t i = 0; i < len; ++i) {
        out.push_back(hex[(bytes[i] >> 4) & 0x0f]);
        out.push_back(hex[bytes[i] & 0x0f]);
    }
    return out;
}

static std::string RandomHex(size_t byte_count)
{
    std::string out;
    out.reserve(byte_count * 2);
    for (size_t i = 0; i < byte_count; ++i) {
        const uint8_t byte = static_cast<uint8_t>(esp_random() & 0xff);
        static constexpr char hex[] = "0123456789abcdef";
        out.push_back(hex[(byte >> 4) & 0x0f]);
        out.push_back(hex[byte & 0x0f]);
    }
    return out;
}

static std::string GetDeviceId()
{
    uint8_t mac[6] = {};
    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) != ESP_OK) {
        return "stackchan-unknown";
    }

    char buffer[32] = {};
    std::snprintf(buffer, sizeof(buffer), "stackchan-%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3],
                  mac[4], mac[5]);
    return buffer;
}

static std::string BuildAuthProofMessage(const std::string& device_id, const std::string& challenge_id,
                                         const std::string& nonce, const std::string& principal)
{
    return device_id + ":" + challenge_id + ":" + nonce + ":" + principal;
}

static std::string HmacSha256Hex(const std::string& key, const std::string& message)
{
    uint8_t digest[32] = {};
    const mbedtls_md_info_t* md_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (md_info == nullptr) {
        return "";
    }

    if (mbedtls_md_hmac(md_info, reinterpret_cast<const unsigned char*>(key.data()), key.size(),
                        reinterpret_cast<const unsigned char*>(message.data()), message.size(), digest) != 0) {
        return "";
    }

    return BytesToHex(digest, sizeof(digest));
}

static bool ConstantTimeEquals(const std::string& a, const std::string& b)
{
    if (a.size() != b.size()) {
        return false;
    }

    uint8_t diff = 0;
    for (size_t i = 0; i < a.size(); ++i) {
        diff |= static_cast<uint8_t>(a[i] ^ b[i]);
    }
    return diff == 0;
}

static bool IsValidPrincipal(const char* principal)
{
    if (principal == nullptr) {
        return false;
    }
    return std::strcmp(principal, "dashboard") == 0 || std::strcmp(principal, "backend_control") == 0 ||
           std::strcmp(principal, "mcp_bridge") == 0 || std::strcmp(principal, "voice_bridge") == 0;
}

static const char* GetJsonString(const cJSON* object, const char* key)
{
    const cJSON* item = cJSON_IsObject(object) ? cJSON_GetObjectItem(object, key) : nullptr;
    return cJSON_IsString(item) ? item->valuestring : nullptr;
}

static const char* GetJsonRpcSessionToken(const cJSON* root, const cJSON* payload)
{
    const char* token = GetJsonString(root, "session_token");
    if (token != nullptr) {
        return token;
    }

    token = GetJsonString(payload, "session_token");
    if (token != nullptr) {
        return token;
    }

    return nullptr;
}

static bool IsLocalControlMethod(const char* method_name)
{
    if (method_name == nullptr) {
        return false;
    }
    return std::strcmp(method_name, "settings/get") == 0 || std::strcmp(method_name, "settings/write_sd") == 0 ||
           std::strcmp(method_name, "local_control/set_token") == 0;
}

static std::string PrintAndDelete(cJSON* json)
{
    if (json == nullptr) {
        return "{}";
    }

    char* printed = cJSON_PrintUnformatted(json);
    std::string result = printed != nullptr ? printed : "{}";
    if (printed != nullptr) {
        cJSON_free(printed);
    }
    cJSON_Delete(json);
    return result;
}

static cJSON* DuplicateJsonRpcId(const cJSON* payload)
{
    const cJSON* id = cJSON_GetObjectItem(payload, "id");
    if (id == nullptr) {
        return cJSON_CreateNull();
    }
    return cJSON_Duplicate(id, true);
}

static std::string JsonRpcResult(const cJSON* payload, cJSON* result)
{
    cJSON* response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "jsonrpc", "2.0");
    cJSON_AddItemToObject(response, "id", DuplicateJsonRpcId(payload));
    cJSON_AddItemToObject(response, "result", result != nullptr ? result : cJSON_CreateObject());
    return PrintAndDelete(response);
}

static std::string JsonRpcError(const cJSON* payload, int code, const char* message)
{
    cJSON* response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "jsonrpc", "2.0");
    cJSON_AddItemToObject(response, "id", DuplicateJsonRpcId(payload));

    cJSON* error = cJSON_CreateObject();
    cJSON_AddNumberToObject(error, "code", code);
    cJSON_AddStringToObject(error, "message", message != nullptr ? message : "error");
    cJSON_AddItemToObject(response, "error", error);

    return PrintAndDelete(response);
}

static cJSON* BuildSettingsResult()
{
    std::string raw_settings;
    std::string effective_settings;
    bool exists = false;

    GetHAL().withSdCard([&]() {
        exists             = hal_bridge::read_sd_settings(raw_settings);
        effective_settings = hal_bridge::get_effective_settings_json();
        return true;
    });

    cJSON* result = cJSON_CreateObject();
    cJSON_AddStringToObject(result, "path", SDCARD_SETTINGS_PATH);
    cJSON_AddBoolToObject(result, "exists", exists);
    if (exists) {
        cJSON_AddStringToObject(result, "raw", raw_settings.c_str());
    }

    cJSON* effective = cJSON_Parse(effective_settings.c_str());
    if (effective != nullptr) {
        cJSON_AddItemToObject(result, "effective", effective);
    }
    return result;
}

static bool ValidateTokenForNvs(const std::string& token, std::string& error_message)
{
    cJSON* root          = cJSON_CreateObject();
    cJSON* local_control = cJSON_CreateObject();
    cJSON_AddStringToObject(local_control, "token", token.c_str());
    cJSON_AddItemToObject(root, "localControl", local_control);

    char* printed = cJSON_PrintUnformatted(root);
    std::string settings_json = printed != nullptr ? printed : "";
    if (printed != nullptr) {
        cJSON_free(printed);
    }
    cJSON_Delete(root);

    return hal_bridge::validate_settings_json(settings_json, nullptr, &error_message);
}

LocalControlWebSocketServer* LocalControlWebSocketServer::instance_ = nullptr;

struct QueuedWsMessage {
    httpd_handle_t server_handle = nullptr;
    int sock_fd                  = -1;
    std::string text;
};

LocalControlWebSocketServer::LocalControlWebSocketServer()
{
    token_   = STACKCHAN_CONTROL_WS_TOKEN;
    instance_ = this;
}

LocalControlWebSocketServer::~LocalControlWebSocketServer()
{
    Stop();
    instance_ = nullptr;
}

bool LocalControlWebSocketServer::Start(int port)
{
    if (server_handle_ != nullptr) {
        return true;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port      = port;
    config.ctrl_port        = 32769;
    config.max_open_sockets = 4;
    config.lru_purge_enable = true;

    httpd_uri_t ws_uri = {
        .uri          = "/ws",
        .method       = HTTP_GET,
        .handler      = WsHandler,
        .user_ctx     = nullptr,
        .is_websocket = true,
    };

    if (httpd_start(&server_handle_, &config) != ESP_OK) {
        ESP_LOGE(TAG, "failed to start WebSocket server on port %d", port);
        server_handle_ = nullptr;
        return false;
    }

    esp_err_t err = httpd_register_uri_handler(server_handle_, &ws_uri);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to register /ws handler: %s", esp_err_to_name(err));
        Stop();
        return false;
    }

    ESP_LOGI(TAG, "WebSocket server started on port %d", port);
    return true;
}

void LocalControlWebSocketServer::Stop()
{
    if (server_handle_ == nullptr) {
        return;
    }

    httpd_stop(server_handle_);
    server_handle_ = nullptr;
    clients_.clear();
    ESP_LOGI(TAG, "WebSocket server stopped");
}

bool LocalControlWebSocketServer::IsRunning() const
{
    return server_handle_ != nullptr;
}

size_t LocalControlWebSocketServer::GetClientCount() const
{
    return clients_.size();
}

void LocalControlWebSocketServer::SetToken(std::string token)
{
    if (token.empty()) {
        token = STACKCHAN_CONTROL_WS_TOKEN;
    }
    token_ = std::move(token);
}

esp_err_t LocalControlWebSocketServer::WsHandler(httpd_req_t* req)
{
    if (instance_ == nullptr) {
        return ESP_FAIL;
    }

    if (req->method == HTTP_GET) {
        instance_->AddClient(req);
        ESP_LOGI(TAG, "WebSocket connection opened; awaiting auth handshake");
        return ESP_OK;
    }

    httpd_ws_frame_t ws_pkt = {};
    ws_pkt.type             = HTTPD_WS_TYPE_TEXT;

    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to read WebSocket frame length: %s", esp_err_to_name(ret));
        return ret;
    }

    if (ws_pkt.type == HTTPD_WS_TYPE_CLOSE) {
        instance_->RemoveClient(req);
        return ESP_OK;
    }

    if (ws_pkt.type != HTTPD_WS_TYPE_TEXT) {
        ESP_LOGW(TAG, "unsupported WebSocket frame type: %d", ws_pkt.type);
        return ESP_OK;
    }

    if (ws_pkt.len == 0) {
        return ESP_OK;
    }

    if (ws_pkt.len > STACKCHAN_CONTROL_WS_MAX_MESSAGE_BYTES) {
        ESP_LOGE(TAG, "message too large: %d bytes", ws_pkt.len);
        instance_->SendText(req, R"({"type":"error","message":"message too large"})");
        return ESP_OK;
    }

    uint8_t* buf = static_cast<uint8_t*>(std::calloc(1, ws_pkt.len + 1));
    if (buf == nullptr) {
        ESP_LOGE(TAG, "failed to allocate WebSocket message buffer");
        return ESP_ERR_NO_MEM;
    }

    ws_pkt.payload = buf;
    ret            = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to read WebSocket payload: %s", esp_err_to_name(ret));
        std::free(buf);
        return ret;
    }

    buf[ws_pkt.len] = '\0';
    instance_->HandleMessage(req, reinterpret_cast<const char*>(buf), ws_pkt.len);
    std::free(buf);
    return ESP_OK;
}

void LocalControlWebSocketServer::HandleMessage(httpd_req_t* req, const char* data, size_t len)
{
    if (data == nullptr || len == 0) {
        SendText(req, R"({"type":"error","message":"empty message"})");
        return;
    }

    cJSON* root = cJSON_ParseWithLength(data, len);
    if (root == nullptr) {
        ESP_LOGE(TAG, "failed to parse JSON control message");
        SendText(req, R"({"type":"error","message":"invalid json"})");
        return;
    }

    cJSON* payload = nullptr;
    cJSON* type    = cJSON_GetObjectItem(root, "type");
    if (cJSON_IsString(type) && std::strcmp(type->valuestring, "mcp") == 0) {
        payload = cJSON_GetObjectItem(root, "payload");
        if (payload != nullptr) {
            cJSON_DetachItemViaPointer(root, payload);
        }
    } else {
        payload = cJSON_Duplicate(root, true);
    }

    if (payload == nullptr) {
        ESP_LOGE(TAG, "control message missing MCP payload");
        SendText(req, R"({"type":"error","message":"missing mcp payload"})");
        cJSON_Delete(root);
        return;
    }

    const int sock_fd = httpd_req_to_sockfd(req);
    if (HandleAuthJsonRpc(req, payload, sock_fd)) {
        cJSON_Delete(payload);
        cJSON_Delete(root);
        return;
    }

    if (!IsClientSessionAuthorized(sock_fd, root, payload)) {
        SendText(req, JsonRpcError(payload, -32001, "unauthenticated local control session").c_str());
        cJSON_Delete(payload);
        cJSON_Delete(root);
        return;
    }

    if (HandleLocalJsonRpc(req, payload)) {
        cJSON_Delete(payload);
        cJSON_Delete(root);
        return;
    }

    const auto client = clients_.find(sock_fd);
    if (client == clients_.end() || !IsPrincipalAllowedMcp(client->second.principal)) {
        SendText(req, JsonRpcError(payload, -32003, "principal is not allowed to use MCP bridge").c_str());
        cJSON_Delete(payload);
        cJSON_Delete(root);
        return;
    }

    McpServer::GetInstance().ParseMessage(payload, [this, sock_fd](const std::string& response) {
        SendText(sock_fd, response);
    });
    cJSON_Delete(payload);
    cJSON_Delete(root);
}

bool LocalControlWebSocketServer::HandleAuthJsonRpc(httpd_req_t* req, const cJSON* payload, int sock_fd)
{
    const cJSON* method = cJSON_GetObjectItem(payload, "method");
    if (!cJSON_IsString(method)) {
        return false;
    }

    const char* method_name = method->valuestring;
    const cJSON* params     = cJSON_GetObjectItem(payload, "params");

    if (std::strcmp(method_name, "local_control/auth_begin") == 0) {
        auto client = clients_.find(sock_fd);
        if (client == clients_.end()) {
            SendText(req, JsonRpcError(payload, -32000, "unknown WebSocket client").c_str());
            return true;
        }

        client->second.authenticated          = false;
        client->second.principal              = "";
        client->second.session_token          = "";
        client->second.challenge_id           = RandomHex(16);
        client->second.nonce                  = RandomHex(16);
        client->second.challenge_expires_at_us = esp_timer_get_time() + AUTH_CHALLENGE_TTL_US;

        cJSON* result = cJSON_CreateObject();
        cJSON_AddStringToObject(result, "algorithm", "hmac-sha256-v1");
        cJSON_AddStringToObject(result, "device_id", GetDeviceId().c_str());
        cJSON_AddStringToObject(result, "challenge_id", client->second.challenge_id.c_str());
        cJSON_AddStringToObject(result, "nonce", client->second.nonce.c_str());
        cJSON_AddNumberToObject(result, "expires_in_ms", AUTH_CHALLENGE_TTL_US / 1000);

        cJSON* principals = cJSON_CreateArray();
        cJSON_AddItemToArray(principals, cJSON_CreateString("dashboard"));
        cJSON_AddItemToArray(principals, cJSON_CreateString("backend_control"));
        cJSON_AddItemToArray(principals, cJSON_CreateString("mcp_bridge"));
        cJSON_AddItemToArray(principals, cJSON_CreateString("voice_bridge"));
        cJSON_AddItemToObject(result, "principals", principals);

        SendText(req, JsonRpcResult(payload, result).c_str());
        return true;
    }

    if (std::strcmp(method_name, "local_control/auth_verify") == 0) {
        auto client = clients_.find(sock_fd);
        if (client == clients_.end()) {
            SendText(req, JsonRpcError(payload, -32000, "unknown WebSocket client").c_str());
            return true;
        }

        const char* challenge_id = GetJsonString(params, "challenge_id");
        const char* principal    = GetJsonString(params, "principal");
        const char* proof        = GetJsonString(params, "proof");
        if (challenge_id == nullptr || principal == nullptr || proof == nullptr) {
            SendText(req, JsonRpcError(payload, -32602, "challenge_id, principal, and proof strings are required").c_str());
            return true;
        }
        if (!IsValidPrincipal(principal)) {
            SendText(req, JsonRpcError(payload, -32602, "invalid principal").c_str());
            return true;
        }
        if (client->second.challenge_id.empty() || std::strcmp(challenge_id, client->second.challenge_id.c_str()) != 0) {
            SendText(req, JsonRpcError(payload, -32002, "unknown auth challenge").c_str());
            return true;
        }
        if (client->second.challenge_expires_at_us <= esp_timer_get_time()) {
            client->second.challenge_id = "";
            client->second.nonce        = "";
            SendText(req, JsonRpcError(payload, -32002, "expired auth challenge").c_str());
            return true;
        }

        const std::string device_id = GetDeviceId();
        const std::string message =
            BuildAuthProofMessage(device_id, client->second.challenge_id, client->second.nonce, principal);
        const std::string expected_proof = HmacSha256Hex(token_, message);
        if (expected_proof.empty() || !ConstantTimeEquals(expected_proof, proof)) {
            client->second.challenge_id = "";
            client->second.nonce        = "";
            ESP_LOGW(TAG, "rejected invalid auth proof for principal %s", principal);
            SendText(req, JsonRpcError(payload, -32002, "invalid auth proof").c_str());
            return true;
        }

        client->second.authenticated           = true;
        client->second.principal               = principal;
        client->second.session_token           = RandomHex(32);
        client->second.challenge_id            = "";
        client->second.nonce                   = "";
        client->second.challenge_expires_at_us = 0;

        cJSON* result = cJSON_CreateObject();
        cJSON_AddStringToObject(result, "session_token", client->second.session_token.c_str());
        cJSON_AddStringToObject(result, "principal", client->second.principal.c_str());
        cJSON_AddStringToObject(result, "device_id", device_id.c_str());
        ESP_LOGI(TAG, "authenticated local control client %d as %s", sock_fd, client->second.principal.c_str());
        SendText(req, JsonRpcResult(payload, result).c_str());
        return true;
    }

    return false;
}

bool LocalControlWebSocketServer::IsClientSessionAuthorized(int sock_fd, const cJSON* root, const cJSON* payload) const
{
    const auto client = clients_.find(sock_fd);
    if (client == clients_.end() || !client->second.authenticated || client->second.session_token.empty()) {
        return false;
    }

    const char* session_token = GetJsonRpcSessionToken(root, payload);
    return session_token != nullptr && ConstantTimeEquals(client->second.session_token, session_token);
}

bool LocalControlWebSocketServer::IsPrincipalAllowedLocalMethod(const std::string& principal, const char* method_name) const
{
    (void)method_name;
    return principal == "dashboard" || principal == "backend_control";
}

bool LocalControlWebSocketServer::IsPrincipalAllowedMcp(const std::string& principal) const
{
    return principal == "dashboard" || principal == "backend_control" || principal == "mcp_bridge";
}

bool LocalControlWebSocketServer::HandleLocalJsonRpc(httpd_req_t* req, const cJSON* payload)
{
    const cJSON* method = cJSON_GetObjectItem(payload, "method");
    if (!cJSON_IsString(method)) {
        return false;
    }

    const char* method_name = method->valuestring;
    const cJSON* params     = cJSON_GetObjectItem(payload, "params");
    if (!IsLocalControlMethod(method_name)) {
        return false;
    }

    const int sock_fd       = httpd_req_to_sockfd(req);
    const auto client       = clients_.find(sock_fd);
    if (client == clients_.end() || !IsPrincipalAllowedLocalMethod(client->second.principal, method_name)) {
        SendText(req, JsonRpcError(payload, -32003, "principal is not allowed to call local method").c_str());
        return true;
    }

    if (std::strcmp(method_name, "settings/get") == 0) {
        SendText(req, JsonRpcResult(payload, BuildSettingsResult()).c_str());
        return true;
    }

    if (std::strcmp(method_name, "settings/write_sd") == 0) {
        const cJSON* settings_json = cJSON_IsObject(params) ? cJSON_GetObjectItem(params, "settings_json") : nullptr;
        if (!cJSON_IsString(settings_json)) {
            SendText(req, JsonRpcError(payload, -32602, "settings_json string is required").c_str());
            return true;
        }

        std::string normalized_json;
        std::string error_message;
        const bool written = GetHAL().withSdCard([&]() {
            return hal_bridge::write_sd_settings(settings_json->valuestring, &normalized_json, &error_message);
        });

        if (!written) {
            const std::string message = "invalid settings: " + error_message;
            SendText(req, JsonRpcError(payload, -32602, message.c_str()).c_str());
            return true;
        }

        cJSON* result = cJSON_CreateObject();
        cJSON_AddStringToObject(result, "path", SDCARD_SETTINGS_PATH);
        cJSON_AddBoolToObject(result, "written", true);
        cJSON_AddBoolToObject(result, "reboot_required", true);

        cJSON* settings = cJSON_ParseWithLength(normalized_json.data(), normalized_json.size());
        if (settings != nullptr) {
            cJSON_AddItemToObject(result, "settings", settings);
        }
        SendText(req, JsonRpcResult(payload, result).c_str());
        return true;
    }

    if (std::strcmp(method_name, "local_control/set_token") == 0) {
        const cJSON* token = cJSON_IsObject(params) ? cJSON_GetObjectItem(params, "token") : nullptr;
        if (!cJSON_IsString(token)) {
            SendText(req, JsonRpcError(payload, -32602, "token string is required").c_str());
            return true;
        }

        std::string error_message;
        if (!ValidateTokenForNvs(token->valuestring, error_message)) {
            const std::string message = "invalid token: " + error_message;
            SendText(req, JsonRpcError(payload, -32602, message.c_str()).c_str());
            return true;
        }

        hal_bridge::set_local_control_token(token->valuestring);

        cJSON* result = cJSON_CreateObject();
        cJSON_AddBoolToObject(result, "written", true);
        cJSON_AddBoolToObject(result, "reboot_required", true);
        cJSON_AddStringToObject(result, "source", "nvs");
        SendText(req, JsonRpcResult(payload, result).c_str());
        return true;
    }

    return false;
}

void LocalControlWebSocketServer::AddClient(httpd_req_t* req)
{
    const int sock_fd = httpd_req_to_sockfd(req);
    clients_[sock_fd] = ClientState{};
    ESP_LOGI(TAG, "client connected: %d (total: %zu)", sock_fd, clients_.size());
}

void LocalControlWebSocketServer::RemoveClient(httpd_req_t* req)
{
    const int sock_fd = httpd_req_to_sockfd(req);
    clients_.erase(sock_fd);
    ESP_LOGI(TAG, "client disconnected: %d (total: %zu)", sock_fd, clients_.size());
}

void LocalControlWebSocketServer::SendText(httpd_req_t* req, const char* text)
{
    httpd_ws_frame_t response = {};
    response.type             = HTTPD_WS_TYPE_TEXT;
    response.payload          = reinterpret_cast<uint8_t*>(const_cast<char*>(text));
    response.len              = std::strlen(text);
    httpd_ws_send_frame(req, &response);
}

void LocalControlWebSocketServer::SendQueuedText(void* arg)
{
    auto* message = static_cast<QueuedWsMessage*>(arg);
    if (message == nullptr) {
        return;
    }

    httpd_ws_frame_t response = {};
    response.type             = HTTPD_WS_TYPE_TEXT;
    response.payload          = reinterpret_cast<uint8_t*>(message->text.data());
    response.len              = message->text.size();

    esp_err_t err = httpd_ws_send_frame_async(message->server_handle, message->sock_fd, &response);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "failed to send queued WebSocket response: %s", esp_err_to_name(err));
    }

    delete message;
}

void LocalControlWebSocketServer::SendText(int sock_fd, const std::string& text)
{
    if (server_handle_ == nullptr || sock_fd < 0) {
        return;
    }

    auto* message = new (std::nothrow) QueuedWsMessage{server_handle_, sock_fd, text};
    if (message == nullptr) {
        ESP_LOGE(TAG, "failed to allocate queued WebSocket response");
        return;
    }

    esp_err_t err = httpd_queue_work(server_handle_, SendQueuedText, message);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "failed to queue WebSocket response: %s", esp_err_to_name(err));
        delete message;
    }
}
