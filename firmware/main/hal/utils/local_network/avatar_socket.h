/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

class NetworkInterface;

namespace local_network {

// Avatar's ws:// links use the existing WebSocket framing over a TCP adapter
// with a shared deadline for every fragment of one outgoing message. Each
// connection attempt owns a fresh socket; callbacks are installed before Connect.
class AvatarSocket {
public:
    static constexpr uint32_t SendTimeoutMs       = 200;
    static constexpr uint32_t RemoteSendTimeoutMs = 1000;
    AvatarSocket(NetworkInterface& network, int connection_id, uint32_t send_timeout_ms = SendTimeoutMs);
    ~AvatarSocket();
    void SetHeader(const char* key, const char* value);
    bool Connect(const char* uri);
    bool IsConnected() const;
    bool Send(const std::string& text);
    bool SendBinary(const uint8_t* data, size_t len);
    void OnConnected(std::function<void()> callback);
    void OnDisconnected(std::function<void()> callback);
    void OnData(std::function<void(const char*, size_t, bool)> callback);
    void OnError(std::function<void(int)> callback);

private:
    struct Impl;
    std::unique_ptr<Impl> _impl;
};

}  // namespace local_network
