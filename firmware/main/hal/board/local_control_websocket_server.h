#ifndef LOCAL_CONTROL_WEBSOCKET_SERVER_H
#define LOCAL_CONTROL_WEBSOCKET_SERVER_H

#include <esp_http_server.h>
#include <cJSON.h>
#include <cstddef>
#include <map>
#include <string>

class LocalControlWebSocketServer {
public:
    LocalControlWebSocketServer();
    ~LocalControlWebSocketServer();

    bool Start(int port = 8080);
    void Stop();
    bool IsRunning() const;
    size_t GetClientCount() const;
    void SetToken(std::string token);

private:
    httpd_handle_t server_handle_ = nullptr;
    std::map<int, bool> clients_;
    std::string token_;

    static LocalControlWebSocketServer* instance_;
    static esp_err_t WsHandler(httpd_req_t* req);
    static void SendQueuedText(void* arg);

    bool IsAuthorized(httpd_req_t* req) const;
    void HandleMessage(httpd_req_t* req, const char* data, size_t len);
    bool HandleLocalJsonRpc(httpd_req_t* req, const cJSON* payload);
    void AddClient(httpd_req_t* req);
    void RemoveClient(httpd_req_t* req);
    void SendText(httpd_req_t* req, const char* text);
    void SendText(int sock_fd, const std::string& text);
};

#endif  // LOCAL_CONTROL_WEBSOCKET_SERVER_H
