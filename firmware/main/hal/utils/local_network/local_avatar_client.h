/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

namespace local_network {
class AvatarSocket;

// The task owns endpoint connection and socket replacement. Callbacks only
// enqueue data; Avatar/Motion/LVGL handlers continue to run on the existing
// Avatar worker.
class AvatarClient {
public:
    using TokenGenerator = std::function<std::string()>;
    using Receiver       = std::function<void(uint32_t, std::vector<uint8_t>)>;

    AvatarClient(std::string factory_mac, TokenGenerator token_generator, Receiver receiver);
    ~AvatarClient();
    bool start();
    bool isConnected() const;
    uint32_t generation() const;
    bool send(uint8_t type, const uint8_t* data, size_t len);

    // The App announces its LAN WebSocket endpoint over the remote channel.
    // Storing it wakes the task so the first attempt happens immediately and
    // later rounds keep retrying the same address every few seconds.
    void setEndpoint(std::vector<std::string> hosts, uint16_t port);

private:
    void run();
    bool connectAnnouncedEndpoint();
    bool hasAnnouncedEndpoint() const;
    bool connect(const std::string& host, uint16_t port);
    void disconnect();
    void markDisconnected(uint32_t generation);

    const std::string _factory_mac;
    TokenGenerator _token_generator;
    Receiver _receiver;
    TaskHandle_t _task         = nullptr;
    SemaphoreHandle_t _stopped = nullptr;
    std::atomic_bool _stopping{false};
    std::atomic_bool _connected{false};
    std::atomic_uint32_t _generation{0};
    std::mutex _socket_mutex;
    std::mutex _send_mutex;
    std::shared_ptr<AvatarSocket> _socket;
    uint64_t _session  = 0;
    uint32_t _sequence = 0;
    mutable std::mutex _endpoint_mutex;
    std::vector<std::string> _endpoint_hosts;
    uint16_t _endpoint_port = 0;
    std::atomic_uint32_t _endpoint_revision{0};
    uint32_t _applied_revision = 0;
};

}  // namespace local_network
