#pragma once

#include "protocol.h"

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>

#include <memory>
#include <string>

#include <web_socket.h>

#define WEBSOCKET_PROTOCOL_AUTH_EVENT (1 << 0)
#define WEBSOCKET_PROTOCOL_SERVER_HELLO_EVENT (1 << 1)

class WebsocketProtocol : public Protocol {
public:
    WebsocketProtocol();
    ~WebsocketProtocol() override;

    bool Start() override;
    bool SendAudio(std::unique_ptr<AudioStreamPacket> packet) override;
    bool OpenAudioChannel() override;
    void CloseAudioChannel(bool send_goodbye = true) override;
    bool IsAudioChannelOpened() const override;

private:
    EventGroupHandle_t event_group_handle_ = nullptr;
    std::unique_ptr<WebSocket> websocket_;
    int version_ = 1;
    bool authenticated_ = false;
    bool server_ready_ = false;
    std::string token_;
    std::string device_nonce_;
    std::string server_nonce_;
    std::string server_id_;

    void ParseServerHello(const cJSON* root);
    void ParseAuthMessage(const cJSON* root);
    bool SendText(const std::string& text) override;
    std::string GetHelloMessage();
    std::string GetAuthBeginMessage();
    std::string BuildAuthProofMessage(const char* role) const;
};
