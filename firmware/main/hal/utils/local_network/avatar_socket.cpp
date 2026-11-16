/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 * SPDX-License-Identifier: MIT
 */
#include "avatar_socket.h"
#include <network_interface.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <climits>
#include <chrono>
#include <cstring>
#include <mutex>
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

namespace local_network {
namespace {
constexpr int ConnectTimeoutMs = 2000;
int64_t deadlineAfter(int ms)
{
    return esp_timer_get_time() + int64_t(ms) * 1000;
}

class BoundedTcp : public Tcp {
public:
    BoundedTcp(std::atomic<int64_t>& deadline, uint32_t timeout_ms)
        : _message_deadline(deadline), _send_timeout_ms(timeout_ms)
    {
    }
    ~BoundedTcp() override
    {
        Disconnect();
        if (_stopped) vSemaphoreDelete(_stopped);
    }

    NetworkResult<> Connect(const std::string& host, int port) override
    {
        Disconnect();
        if (!_stopped) _stopped = xSemaphoreCreateBinary();
        if (!_stopped) return std::unexpected(NetworkError::FromErrno(ENOMEM));
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port   = htons(port);
        if (inet_pton(AF_INET, host.c_str(), &address.sin_addr) != 1) {
            addrinfo hints{};
            hints.ai_family   = AF_INET;
            hints.ai_socktype = SOCK_STREAM;
            addrinfo* result  = nullptr;
            // Avatar connections run in their connection task. LAN announcements
            // carry numeric IPv4 addresses and do not need a DNS lookup.
            if (getaddrinfo(host.c_str(), nullptr, &hints, &result) != 0 || !result) {
                _last_error = EHOSTUNREACH;
                return std::unexpected(NetworkError::DnsFailed(EHOSTUNREACH));
            }
            address.sin_addr = reinterpret_cast<sockaddr_in*>(result->ai_addr)->sin_addr;
            freeaddrinfo(result);
        }
        _fd = socket(AF_INET, SOCK_STREAM, 0);
        if (_fd < 0) {
            _last_error = errno;
            return std::unexpected(NetworkError::FromErrno(errno));
        }
        const int flags = fcntl(_fd, F_GETFL, 0);
        if (flags < 0 || fcntl(_fd, F_SETFL, flags | O_NONBLOCK) < 0) return connectFailed(errno);
        if (::connect(_fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
            if (errno != EINPROGRESS || !waitWritable(deadlineAfter(ConnectTimeoutMs))) return connectFailed(errno);
            int error      = 0;
            socklen_t size = sizeof(error);
            if (getsockopt(_fd, SOL_SOCKET, SO_ERROR, &error, &size) < 0) return connectFailed(errno);
            if (error) return connectFailed(error);
        }
        _running          = true;
        connected_        = true;
        _stream_ready     = false;
        _disconnect_ready = false;
        TaskHandle_t task = nullptr;
        if (xTaskCreate(
                [](void* argument) {
                    auto* tcp = static_cast<BoundedTcp*>(argument);
                    tcp->receive();
                    xSemaphoreGive(tcp->_stopped);
                    vTaskDelete(nullptr);
                },
                "avatar_rx", 4096, this, 1, &task) != pdPASS) {
            _running = false;
            return connectFailed(ENOMEM);
        }
        _has_task = true;
        return {};
    }

    void Disconnect() override
    {
        Abort();
        if (_has_task) {
            xSemaphoreTake(_stopped, portMAX_DELAY);
            _has_task = false;
        }
        if (_fd >= 0) {
            close(_fd);
            _fd = -1;
        }
        connected_ = false;
    }

    void Abort(int error = 0)
    {
        if (error) _last_error = error;
        if (_running.exchange(false)) {
            // Do not close/reuse the descriptor while receive() may still use it.
            shutdown(_fd, SHUT_RDWR);
            std::function<void()> callback;
            {
                std::lock_guard<std::mutex> lock(_callbacks);
                callback = disconnect_callback_;
            }
            if (callback) callback();
        }
    }

    int Send(const std::string& data) override
    {
        if (!_running || data.size() > INT_MAX) return -1;
        const auto budget      = _message_deadline.load();
        const int64_t deadline = budget ? budget : deadlineAfter(_send_timeout_ms);
        size_t sent            = 0;
        while (sent < data.size() && _running) {
            if (!waitWritable(deadline)) {
                Abort(errno);
                return -1;
            }
            const auto result = ::send(_fd, data.data() + sent, data.size() - sent, MSG_DONTWAIT);
            if (result > 0) {
                sent += result;
                continue;
            }
            if (result < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) continue;
            Abort(result == 0 ? EPIPE : errno);
            return -1;  // A zero/partial write is never a successful WebSocket frame.
        }
        return sent == data.size() ? static_cast<int>(sent) : -1;
    }

    bool IsOpen() const
    {
        return _running;
    }

    void OnStream(std::function<void(const std::string&)> callback) override
    {
        std::lock_guard<std::mutex> lock(_callbacks);
        stream_callback_ = std::move(callback);
        _stream_ready    = true;
    }
    void OnDisconnected(std::function<void()> callback) override
    {
        std::lock_guard<std::mutex> lock(_callbacks);
        disconnect_callback_ = std::move(callback);
        _disconnect_ready    = true;
    }

private:
    std::atomic<int64_t>& _message_deadline;
    const uint32_t _send_timeout_ms;
    std::atomic<bool> _running{false}, _stream_ready{false}, _disconnect_ready{false};
    std::atomic<int> _last_error{0};
    int _fd                    = -1;
    SemaphoreHandle_t _stopped = nullptr;
    bool _has_task             = false;
    std::mutex _callbacks;

    NetworkResult<> connectFailed(int error)
    {
        _last_error = error;
        if (_fd >= 0) {
            close(_fd);
            _fd = -1;
        }
        connected_ = false;
        return std::unexpected(NetworkError::FromErrno(error));
    }
    bool waitWritable(int64_t deadline)
    {
        while (true) {
            const auto remaining = deadline - esp_timer_get_time();
            if (remaining <= 0) {
                errno = ETIMEDOUT;
                return false;
            }
            fd_set writable;
            FD_ZERO(&writable);
            FD_SET(_fd, &writable);
            timeval timeout{static_cast<long>(remaining / 1000000), static_cast<long>(remaining % 1000000)};
            const int result = select(_fd + 1, nullptr, &writable, nullptr, &timeout);
            if (result > 0) return true;
            if (result < 0 && errno == EINTR) continue;
            if (result == 0) errno = ETIMEDOUT;
            return false;
        }
    }
    void receive()
    {
        while (_running) {
            // The upstream WebSocket registers callbacks after writing its HTTP
            // request. Do not consume an early handshake response before that.
            if (!_stream_ready || !_disconnect_ready) {
                vTaskDelay(pdMS_TO_TICKS(1));
                continue;
            }
            fd_set readable;
            FD_ZERO(&readable);
            FD_SET(_fd, &readable);
            timeval timeout{0, 100000};
            const int ready = select(_fd + 1, &readable, nullptr, nullptr, &timeout);
            if (ready == 0 || (ready < 0 && errno == EINTR)) continue;
            if (ready < 0) {
                Abort(errno);
                break;
            }
            std::string bytes(1500, '\0');
            const auto count = recv(_fd, bytes.data(), bytes.size(), MSG_DONTWAIT);
            if (count < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) continue;
            if (count <= 0) {
                Abort(count == 0 ? 0 : errno);
                break;
            }
            bytes.resize(count);
            std::function<void(const std::string&)> callback;
            {
                std::lock_guard<std::mutex> lock(_callbacks);
                callback = stream_callback_;
            }
            if (callback) callback(bytes);
        }
    }
};

class SocketNetwork : public NetworkInterface {
public:
    SocketNetwork(NetworkInterface& parent, uint32_t timeout_ms) : _parent(parent), _send_timeout_ms(timeout_ms)
    {
    }
    std::atomic<int64_t> deadline{0};
    BoundedTcp* tcp = nullptr;
    std::unique_ptr<Tcp> CreateTcp(int) override
    {
        auto result = std::make_unique<BoundedTcp>(deadline, _send_timeout_ms);
        tcp         = result.get();
        return result;
    }
    std::unique_ptr<Http> CreateHttp(int id) override
    {
        return _parent.CreateHttp(id);
    }
    std::unique_ptr<Tcp> CreateSsl(int id) override
    {
        return _parent.CreateSsl(id);
    }
    std::unique_ptr<Udp> CreateUdp(int id) override
    {
        return _parent.CreateUdp(id);
    }
    std::unique_ptr<Mqtt> CreateMqtt(int id) override
    {
        return _parent.CreateMqtt(id);
    }
    std::unique_ptr<WebSocket> CreateWebSocket(int id) override
    {
        return std::make_unique<WebSocket>(this, id);
    }

private:
    NetworkInterface& _parent;
    const uint32_t _send_timeout_ms;
};
}  // namespace

struct AvatarSocket::Impl {
    Impl(NetworkInterface& parent, int id, uint32_t timeout)
        : network(parent, timeout), socket(network.CreateWebSocket(id)), timeout_ms(timeout)
    {
        socket->OnConnected([this] {
            connected = true;
            if (on_connected) on_connected();
        });
        socket->OnDisconnected([this] {
            connected = false;
            if (on_disconnected) on_disconnected();
        });
        socket->OnError([this](const NetworkError& error) {
            connected = false;
            if (on_error) on_error(error.native);
        });
    }
    ~Impl()
    {
        // Join TCP callbacks before the upstream WebSocket frees its receive
        // buffers and handshake event group (including failed handshakes).
        if (network.tcp) network.tcp->Disconnect();
        socket.reset();
    }
    SocketNetwork network;
    std::unique_ptr<WebSocket> socket;
    std::atomic<bool> connected{false};
    std::timed_mutex sending;
    const uint32_t timeout_ms;
    std::function<void()> on_connected, on_disconnected;
    std::function<void(int)> on_error;

    template <class Sender>
    bool send(Sender sender)
    {
        std::unique_lock<std::timed_mutex> lock(sending, std::defer_lock);
        if (!lock.try_lock_for(std::chrono::milliseconds(timeout_ms))) {
            connected = false;
            if (network.tcp) network.tcp->Abort(ETIMEDOUT);
            return false;
        }
        if (!connected || (network.tcp && !network.tcp->IsOpen())) return false;
        network.deadline  = deadlineAfter(timeout_ms);
        const bool result = sender();
        network.deadline  = 0;
        if (!result) {
            connected = false;
            if (network.tcp) network.tcp->Abort(ETIMEDOUT);
        }
        return result;
    }
};

AvatarSocket::AvatarSocket(NetworkInterface& network, int id, uint32_t timeout_ms)
    : _impl(std::make_unique<Impl>(network, id, timeout_ms))
{
}
AvatarSocket::~AvatarSocket() = default;
void AvatarSocket::SetHeader(const char* key, const char* value)
{
    _impl->socket->SetHeader(key, value);
}
bool AvatarSocket::Connect(const char* uri)
{
    const bool result = _impl->socket->Connect(uri) && IsConnected();
    if (!result) {
        _impl->connected = false;
        if (_impl->network.tcp) _impl->network.tcp->Disconnect();
    }
    return result;
}
bool AvatarSocket::IsConnected() const
{
    return _impl->connected && (!_impl->network.tcp || _impl->network.tcp->IsOpen());
}
bool AvatarSocket::Send(const std::string& text)
{
    return _impl->send([&] { return _impl->socket->Send(text); });
}
bool AvatarSocket::SendBinary(const uint8_t* data, size_t len)
{
    if (!data && len != 0) return false;
    return _impl->send([&] {
        size_t offset = 0;
        do {
            if (esp_timer_get_time() >= _impl->network.deadline) return false;
            const auto size = std::min<size_t>(len - offset, 32768);
            if (!_impl->socket->Send(data ? data + offset : nullptr, size, offset == 0, offset + size == len))
                return false;
            offset += size;
        } while (offset < len);
        return true;
    });
}
void AvatarSocket::OnConnected(std::function<void()> cb)
{
    _impl->on_connected = std::move(cb);
}
void AvatarSocket::OnDisconnected(std::function<void()> cb)
{
    _impl->on_disconnected = std::move(cb);
}
void AvatarSocket::OnData(std::function<void(const char*, size_t, bool)> cb)
{
    _impl->socket->OnData(std::move(cb));
}
void AvatarSocket::OnError(std::function<void(int)> cb)
{
    _impl->on_error = std::move(cb);
}
}  // namespace local_network
