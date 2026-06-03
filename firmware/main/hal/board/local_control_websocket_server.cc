#include "local_control_websocket_server.h"

#include "config.h"
#include "mcp_server.h"

#include <cJSON.h>
#include <esp_log.h>
#include <cstdlib>
#include <cstring>
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

LocalControlWebSocketServer* LocalControlWebSocketServer::instance_ = nullptr;

struct QueuedWsMessage {
    httpd_handle_t server_handle = nullptr;
    int sock_fd                  = -1;
    std::string text;
};

LocalControlWebSocketServer::LocalControlWebSocketServer()
{
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

bool LocalControlWebSocketServer::IsAuthorized(httpd_req_t* req) const
{
    const char* expected_token = STACKCHAN_CONTROL_WS_TOKEN;
    if (expected_token == nullptr || expected_token[0] == '\0') {
        ESP_LOGW(TAG, "control WebSocket token is empty; rejecting connection");
        return false;
    }

    char query[128] = {};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char token[96] = {};
        if (httpd_query_key_value(query, "token", token, sizeof(token)) == ESP_OK &&
            std::strcmp(token, expected_token) == 0) {
            return true;
        }
    }

    const size_t auth_len = httpd_req_get_hdr_value_len(req, "Authorization");
    if (auth_len > 0 && auth_len < 128) {
        char auth[128] = {};
        if (httpd_req_get_hdr_value_str(req, "Authorization", auth, sizeof(auth)) == ESP_OK) {
            if (std::strcmp(auth, expected_token) == 0) {
                return true;
            }

            const char bearer_prefix[] = "Bearer ";
            if (std::strncmp(auth, bearer_prefix, sizeof(bearer_prefix) - 1) == 0 &&
                std::strcmp(auth + sizeof(bearer_prefix) - 1, expected_token) == 0) {
                return true;
            }
        }
    }

    return false;
}

esp_err_t LocalControlWebSocketServer::WsHandler(httpd_req_t* req)
{
    if (instance_ == nullptr) {
        return ESP_FAIL;
    }

    if (req->method == HTTP_GET) {
        if (!instance_->IsAuthorized(req)) {
            ESP_LOGW(TAG, "rejected unauthorized WebSocket connection");
            httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
            return ESP_FAIL;
        }

        instance_->AddClient(req);
        ESP_LOGI(TAG, "WebSocket connection opened");
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
    McpServer::GetInstance().ParseMessage(payload, [this, sock_fd](const std::string& response) {
        SendText(sock_fd, response);
    });
    cJSON_Delete(payload);
    cJSON_Delete(root);
}

void LocalControlWebSocketServer::AddClient(httpd_req_t* req)
{
    const int sock_fd = httpd_req_to_sockfd(req);
    clients_[sock_fd] = true;
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
