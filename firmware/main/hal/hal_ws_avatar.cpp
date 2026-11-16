/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal.h"
#include <stackchan/stackchan.h>
#include "board/hal_bridge.h"
#include <mooncake.h>
#include <mooncake_log.h>
#include <board.h>
#include "utils/local_network/avatar_socket.h"
#include <esp_log.h>
#include <jpg/image_to_jpeg.h>
#include <ArduinoJson.hpp>
#include <settings.h>
#include <mutex>
#include <queue>
#include <vector>
#include <esp_heap_caps.h>
#include <display.h>
#include <lvgl_image.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <atomic>
#include "utils/jpeg_to_image/jpeg_decoder.h"
#include "utils/secret_logic/secret_logic.h"
#include "utils/local_network/local_avatar_client.h"
#include "utils/local_network/local_network_protocol.h"

static std::string _tag = "WS-Avatar";

static const std::string _setting_ns              = "stackchan";
static const std::string _setting_device_name_key = "device_name";

class WebSocketAvatar {
public:
    ~WebSocketAvatar()
    {
        _stopping = true;
        if (_call_response_connection >= 0) GetHAL().onWsCallResponse.disconnect(_call_response_connection);
        if (_call_end_connection >= 0) GetHAL().onWsCallEnd.disconnect(_call_end_connection);
        _local_client.reset();
        stopRemoteTask();
        _websocket.reset();
    }

    enum class DataType : uint8_t {
        Opus              = 0x01,
        Jpeg              = 0x02,
        ControlAvatar     = 0x03,
        ControlMotion     = 0x04,
        StartCameraStream = 0x05,
        StopCameraStream  = 0x06,
        TextMessage       = 0x07,
        RequestCall       = 0x09,
        DeclineCall       = 0x0A,
        AcceptCall        = 0x0B,
        EndCall           = 0x0C,
        SetDeviceName     = 0x0D,
        GetDeviceName     = 0x0E,
        HeartbeatPing     = 0x10,
        HeartbeatPong     = 0x11,
        VideoModeOn       = 0x12,
        VideoModeOff      = 0x13,
        DanceSequence     = 0x14,
        StartAudioStream  = 0x18,
        StopAudioStream   = 0x19,
        AimedTakePhoto    = 0x1A,
        AppLanAddress     = 0x1B,
    };

    enum class Channel : uint8_t {
        Remote,
        Local,
    };

    struct ReceivedMessage {
        Channel channel;
        uint32_t generation;
        bool binary;
        std::vector<uint8_t> data;
    };

    void init()
    {
        _url = fmt::format("{}/stackChan/ws?deviceType=StackChan", secret_logic::get_server_url());

        _local_client = std::make_unique<local_network::AvatarClient>(
            GetHAL().getFactoryMacString(), []() { return secret_logic::generate_handshake_token("stackchan-lan-v1"); },
            [this](uint32_t generation, std::vector<uint8_t> frame) {
                enqueueMessage({Channel::Local, generation, true, std::move(frame)});
            });
        _local_client->start();

        _call_response_connection = GetHAL().onWsCallResponse.connect([this](bool accepted) {
            if (!isConnected()) {
                return;
            }

            if (accepted) {
                ESP_LOGI(_tag.c_str(), "Sending AcceptCall");
                sendPacket(_call_channel, DataType::AcceptCall, nullptr, 0);
            } else {
                ESP_LOGI(_tag.c_str(), "Sending DeclineCall");
                sendPacket(_call_channel, DataType::DeclineCall, nullptr, 0);
            }
        });

        _call_end_connection = GetHAL().onWsCallEnd.connect([this](WsSignalSource source) {
            if (!isConnected()) {
                return;
            }

            if (source != WsSignalSource::Local) {
                return;
            }

            ESP_LOGI(_tag.c_str(), "Sending EndCall");
            sendPacket(_call_channel, DataType::EndCall, nullptr, 0);
        });

        startRemoteTask();
    }

    void connect()
    {
        const auto token = secret_logic::generate_auth_token();
        if (_stopping) return;
        auto socket           = std::make_shared<local_network::AvatarSocket>(*Board::GetInstance().GetNetwork(), 1,
                                                                              local_network::AvatarSocket::RemoteSendTimeoutMs);
        const auto generation = ++_remote_generation;
        _remote_connected     = false;
        socket->SetHeader("Authorization", token.c_str());
        socket->OnConnected([this, generation]() {
            if (_stopping || generation != _remote_generation) return;
            _last_heartbeat_time = GetHAL().millis();
            _remote_connected    = true;
            ESP_LOGI(_tag.c_str(), "Connected to server");
        });
        socket->OnDisconnected([this, generation]() {
            if (generation == _remote_generation) _remote_connected = false;
            ESP_LOGI(_tag.c_str(), "Server disconnected");
        });
        socket->OnData([this, generation](const char* data, size_t len, bool binary) {
            if (_stopping || generation != _remote_generation || len == 0 || len > 256 * 1024) return;
            enqueueMessage({Channel::Remote, generation, binary, std::vector<uint8_t>(data, data + len)});
        });
        std::shared_ptr<local_network::AvatarSocket> old;
        {
            std::lock_guard<std::mutex> lock(_remote_mutex);
            old        = std::move(_websocket);
            _websocket = socket;
        }
        old.reset();
        if (!socket->Connect(_url.c_str())) {
            _remote_connected = false;
            ESP_LOGW(_tag.c_str(), "Connect to server failed; the LAN connection task continues");
        } else {
            std::lock_guard<std::mutex> lock(_send_mutex);
            socket->Send("{\"type\":\"hello\", \"msg\":\"Hello from StackChan!\"}");
        }
    }

    void update()
    {
        // The endpoint announcement and both connection attempts run outside
        // this worker, so an unavailable server never blocks LAN controls or
        // camera capture.
        processMessages();
        if (_remote_connected && GetHAL().millis() - _last_heartbeat_time > 10000) {
            GetHAL().onWsLog.emit(CommonLogLevel::Error, "Heartbeat Timeout");
            _last_heartbeat_time = GetHAL().millis();
        }
        if (_is_streaming && GetHAL().millis() - _last_capture_time >= (_is_video_mode ? 700 : 350)) {
            captureAndSendFrame();
            _last_capture_time = GetHAL().millis();
        }
    }

    void processMessages()
    {
        std::vector<ReceivedMessage> messages;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            while (!_msg_queue.empty()) {
                messages.push_back(std::move(_msg_queue.front()));
                _msg_queue.pop();
            }
            _queued_bytes = 0;
        }

        for (const auto& msg : messages) {
            const auto generation =
                msg.channel == Channel::Local ? _local_client->generation() : _remote_generation.load();
            if (msg.generation != generation) continue;
            handleMessage(msg);
        }
    }

    void handleMessage(const ReceivedMessage& msg)
    {
        // Media and controls over the LAN prove the session is alive, so a busy
        // LAN link must not be reported as a remote heartbeat timeout.
        _last_heartbeat_time = GetHAL().millis();
        if (msg.binary) {
            if (msg.data.size() < 1) return;
            DataType type = static_cast<DataType>(msg.data[0]);
            ESP_LOGI(_tag.c_str(), "Received binary type: %d, len: %d", (int)type, (int)msg.data.size());

            switch (type) {
                // Opus handled in OnData Fast Path
                // case DataType::Opus: {
                //     if (msg.data.size() > 5) {
                //         auto packet = std::make_unique<AudioStreamPacket>();
                //         packet->payload.assign(msg.data.begin() + 5, msg.data.end());
                //         _audio_service.PushPacketToDecodeQueue(std::move(packet));
                //     }
                //     break;
                // }
                case DataType::StartCameraStream: {
                    ESP_LOGI(_tag.c_str(), "Start Camera Stream");
                    setStreamingEnabled(true);
                    sendText(msg.channel, "camera stream started");
                    break;
                }
                case DataType::StopCameraStream: {
                    ESP_LOGI(_tag.c_str(), "Stop Camera Stream");
                    setStreamingEnabled(false);
                    sendText(msg.channel, "camera stream stopped");
                    break;
                }
                case DataType::ControlAvatar: {
                    // Protocol: [Type(1)] [Length(4)] [Payload]
                    if (msg.data.size() >= 5) {
                        std::string payload(msg.data.begin() + 5, msg.data.end());
                        // ESP_LOGI(_tag.c_str(), "Control Avatar Payload: %s", payload.c_str());
                        GetHAL().onWsAvatarData.emit(payload);
                    }
                    break;
                }
                case DataType::ControlMotion: {
                    // Protocol: [Type(1)] [Length(4)] [Payload]
                    if (msg.data.size() >= 5) {
                        std::string payload(msg.data.begin() + 5, msg.data.end());
                        // ESP_LOGI(_tag.c_str(), "Control Motion Payload: %s", payload.c_str());
                        GetHAL().onWsMotionData.emit(payload);
                    }
                    break;
                }
                case DataType::RequestCall: {
                    // Protocol: [Type(1)] [Length(4)] [Payload]
                    if (msg.data.size() >= 5) {
                        _call_channel = msg.channel;
                        std::string payload(msg.data.begin() + 5, msg.data.end());
                        ESP_LOGI(_tag.c_str(), "RequestCall received, payload size: %u", (unsigned)payload.size());
                        GetHAL().onWsCallRequest.emit(payload);
                    }
                    break;
                }
                case DataType::EndCall: {
                    ESP_LOGI(_tag.c_str(), "EndCall");
                    GetHAL().onWsCallEnd.emit(WsSignalSource::Remote);
                    break;
                }
                case DataType::SetDeviceName: {
                    // Protocol: [Type(1)] [Length(4)] [Payload]
                    if (msg.data.size() >= 5) {
                        std::string payload(msg.data.begin() + 5, msg.data.end());
                        ESP_LOGI(_tag.c_str(), "SetDeviceName received, payload size: %u", (unsigned)payload.size());

                        Settings settings(_setting_ns, true);
                        settings.SetString(_setting_device_name_key, payload);
                    }
                    break;
                }
                case DataType::GetDeviceName: {
                    ESP_LOGI(_tag.c_str(), "GetDeviceName");

                    Settings settings(_setting_ns, false);
                    auto device_name = settings.GetString(_setting_device_name_key, "StackChan");

                    sendPacket(msg.channel, DataType::GetDeviceName, (const uint8_t*)device_name.c_str(),
                               device_name.size());
                    break;
                }
                case DataType::HeartbeatPing: {
                    ESP_LOGI(_tag.c_str(), "HeartbeatPing");
                    if (msg.channel == Channel::Remote) {
                        _last_heartbeat_time = GetHAL().millis();
                    }
                    sendPacket(msg.channel, DataType::HeartbeatPong, nullptr, 0);
                    break;
                }
                case DataType::TextMessage: {
                    // Protocol: [Type(1)] [Length(4)] [Payload]
                    if (msg.data.size() >= 5) {
                        std::string payload(msg.data.begin() + 5, msg.data.end());
                        ESP_LOGI(_tag.c_str(), "TextMessage received, payload size: %u", (unsigned)payload.size());

                        ArduinoJson::JsonDocument doc;
                        auto error = ArduinoJson::deserializeJson(doc, payload);
                        if (error) {
                            ESP_LOGE(_tag.c_str(), "DeserializeJson failed: %s", error.c_str());
                            return;
                        }

                        WsTextMessage_t text_msg;

                        if (doc["name"].is<std::string>()) {
                            text_msg.name = doc["name"].as<std::string>();
                        }
                        if (doc["content"].is<std::string>()) {
                            text_msg.content = doc["content"].as<std::string>();
                        }

                        GetHAL().onWsTextMessage.emit(text_msg);
                    }
                    break;
                }
                case DataType::VideoModeOn: {
                    ESP_LOGI(_tag.c_str(), "VideoModeOn");
                    GetHAL().onWsVideoModeChange.emit(true);
                    _is_video_mode = true;
                    break;
                }
                case DataType::VideoModeOff: {
                    ESP_LOGI(_tag.c_str(), "VideoModeOff");
                    GetHAL().onWsVideoModeChange.emit(false);
                    _is_video_mode = false;
                    break;
                }
                case DataType::Jpeg: {
                    // Protocol: [Type(1)] [Length(4)] [Payload]
                    if (msg.data.size() >= 5) {
                        ESP_LOGI(_tag.c_str(), "Jpeg Frame Received, size: %d", (int)(msg.data.size() - 5));

                        static int64_t _time_count = 0;
                        static int64_t _interval   = 0;
                        _time_count                = esp_timer_get_time();

                        size_t jpeg_len    = msg.data.size() - 5;
                        uint8_t* jpeg_data = (uint8_t*)heap_caps_malloc(jpeg_len, MALLOC_CAP_8BIT);
                        if (jpeg_data) {
                            memcpy(jpeg_data, msg.data.data() + 5, jpeg_len);

                            auto image = jpeg_dec::decode_to_lvgl(jpeg_data, jpeg_len);
                            if (image) {
                                // ESP_LOGI(_tag.c_str(), "Done");

                                _interval = esp_timer_get_time() - _time_count;
                                mclog::info("jpeg decode time: {} ms", _interval / 1000);

                                GetHAL().onWsVideoFrame.emit(image);
                            } else {
                                ESP_LOGE(_tag.c_str(), "Failed to decode JPEG");
                            }
                            heap_caps_free(jpeg_data);
                        } else {
                            ESP_LOGE(_tag.c_str(), "Failed to allocate memory for JPEG");
                        }
                    }
                    break;
                }
                case DataType::DanceSequence: {
                    // Protocol: [Type(1)] [Length(4)] [Payload]
                    if (msg.data.size() >= 5) {
                        std::string payload(msg.data.begin() + 5, msg.data.end());
                        // ESP_LOGI(_tag.c_str(), "Dance Payload:\n%s", payload.c_str());
                        ESP_LOGI(_tag.c_str(), "DanceSequence size: %d", (int)payload.size());
                        GetHAL().onWsDanceData.emit(payload);
                    }
                    break;
                }
                case DataType::AppLanAddress: {
                    // Protocol: [Type(1)] [Length(4)] [JSON]
                    if (msg.data.size() >= 5) {
                        const std::string payload(msg.data.begin() + 5, msg.data.end());
                        applyAnnouncedEndpoint(payload);
                    }
                    break;
                }
                case DataType::StartAudioStream: {
                    break;
                }
                case DataType::StopAudioStream: {
                    break;
                }
                default:
                    break;
            }
        } else {
            ESP_LOGI(_tag.c_str(), "Received text, size: %u", (unsigned)msg.data.size());
        }
    }

    /// The App announces its LAN endpoint over the remote channel; connect on
    /// receipt instead of waiting for the next retry round.
    void applyAnnouncedEndpoint(const std::string& payload)
    {
        std::vector<std::string> hosts;
        uint16_t port = 0;
        if (!local_network::parseEndpointAnnouncement(payload, hosts, port)) {
            ESP_LOGW(_tag.c_str(), "Ignoring AppLanAddress without a usable IPv4 endpoint");
            return;
        }
        ESP_LOGI(_tag.c_str(), "App announced LAN endpoint: %u address(es), port %u", (unsigned)hosts.size(),
                 (unsigned)port);
        _local_client->setEndpoint(std::move(hosts), port);
    }

    bool isConnected()
    {
        return (_local_client && _local_client->isConnected()) || _remote_connected;
    }

    void captureAndSendFrame()
    {
        if (!isConnected()) {
            return;
        }

        static int64_t _time_count = 0;
        static int64_t _interval   = 0;

        auto camera = hal_bridge::board_get_camera();
        if (!camera) {
            return;
        }

        // AI capture/upload owns the same frame. Skip video instead of blocking
        // the HAL loop or encoding a frame that another task is replacing.
        auto frame_lock = camera->TryLockFrame();
        if (!frame_lock.owns_lock()) return;
        _time_count = esp_timer_get_time();
        if (camera->StreamCaptures()) {
            _interval = esp_timer_get_time() - _time_count;
            mclog::info("camera capture time: {} ms", _interval / 1000);

            const uint8_t* frameData = camera->GetFrameData();
            size_t frameSize         = camera->GetFrameSize();
            int width                = camera->GetFrameWidth();
            int height               = camera->GetFrameHeight();
            int format               = camera->GetFrameFormat();

            // Keep LAN JPEG inside the uint16 payload limit. Sending an
            // oversized frame to remote while LAN is active would be ignored
            // by the App's LAN-priority media listener.
            for (const int quality : {20, 10, 5}) {
                uint8_t* jpeg_data   = nullptr;
                size_t jpeg_len      = 0;
                const bool encoded   = image_to_jpeg((uint8_t*)frameData, frameSize, width, height,
                                                     (v4l2_pix_fmt_t)format, quality, &jpeg_data, &jpeg_len);
                const bool too_large = _local_client->isConnected() && jpeg_len > local_network::MaxPayloadSize;
                if (encoded && jpeg_data && !too_large) sendPacket(DataType::Jpeg, jpeg_data, jpeg_len);
                free(jpeg_data);
                if (!encoded || !too_large) break;
                if (quality == 5) ESP_LOGW(_tag.c_str(), "Skipping oversized LAN JPEG");
            }
        }
    }

    void setStreamingEnabled(bool enabled)
    {
        _is_streaming = enabled;
    }

private:
    std::shared_ptr<local_network::AvatarSocket> _websocket;
    std::unique_ptr<local_network::AvatarClient> _local_client;
    std::mutex _remote_mutex;
    TaskHandle_t _remote_task         = nullptr;
    SemaphoreHandle_t _remote_stopped = nullptr;
    std::atomic_bool _stopping{false};
    std::atomic_bool _remote_connected{false};
    std::atomic_uint32_t _remote_generation{0};
    std::atomic_uint32_t _last_heartbeat_time{0};
    std::string _url;
    uint32_t _last_capture_time   = 0;
    bool _is_streaming            = false;
    bool _is_video_mode           = false;
    Channel _call_channel         = Channel::Remote;
    int _call_response_connection = -1;
    int _call_end_connection      = -1;
    std::mutex _mutex;
    std::queue<ReceivedMessage> _msg_queue;
    size_t _queued_bytes = 0;
    std::mutex _send_mutex;

    void enqueueMessage(ReceivedMessage message)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        // A delayed UI/camera decoder must not allow media to exhaust the heap.
        if (_msg_queue.size() >= 16 || _queued_bytes + message.data.size() > 256 * 1024) return;
        _queued_bytes += message.data.size();
        _msg_queue.push(std::move(message));
    }

    void startRemoteTask()
    {
        _remote_stopped = xSemaphoreCreateBinary();
        if (_remote_stopped == nullptr) return;
        if (xTaskCreate(
                [](void* argument) {
                    auto* service         = static_cast<WebSocketAvatar*>(argument);
                    bool first_attempt    = true;
                    uint32_t last_attempt = 0;
                    while (!service->_stopping) {
                        if (!service->_remote_connected &&
                            (first_attempt || GetHAL().millis() - last_attempt >= 5000)) {
                            first_attempt = false;
                            service->connect();
                            last_attempt = GetHAL().millis();
                        }
                        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(250));
                    }
                    xSemaphoreGive(service->_remote_stopped);
                    vTaskDelete(nullptr);
                },
                "avatar_remote", 8192, this, 2, &_remote_task) != pdPASS) {
            vSemaphoreDelete(_remote_stopped);
            _remote_stopped = nullptr;
            _remote_task    = nullptr;
            ESP_LOGE(_tag.c_str(), "Failed to start remote connection task");
        }
    }

    void stopRemoteTask()
    {
        _stopping = true;
        if (_remote_task != nullptr) {
            xTaskNotifyGive(_remote_task);
            xSemaphoreTake(_remote_stopped, portMAX_DELAY);
        }
        if (_remote_stopped != nullptr) vSemaphoreDelete(_remote_stopped);
    }

    std::shared_ptr<local_network::AvatarSocket> remoteSocket()
    {
        std::lock_guard<std::mutex> lock(_remote_mutex);
        return _websocket;
    }

    void sendText(Channel channel, const char* text)
    {
        // The App's LAN interface accepts binary messages only; the legacy
        // text acknowledgements are retained for remote camera commands.
        if (channel != Channel::Remote || !_remote_connected) return;
        std::lock_guard<std::mutex> lock(_send_mutex);
        if (auto socket = remoteSocket()) socket->Send(text);
    }

    bool sendRemotePacket(DataType type, const uint8_t* data, size_t len)
    {
        if (!_remote_connected) return false;
        auto socket = remoteSocket();
        if (!socket) return false;
        std::vector<uint8_t> packet(5 + len);
        packet[0] = static_cast<uint8_t>(type);
        local_network::writeBigEndian(packet.data() + 1, len, 4);
        if (len != 0) memcpy(packet.data() + 5, data, len);
        if (socket->SendBinary(packet.data(), packet.size())) return true;
        _remote_connected = false;
        return false;
    }

    void sendPacket(Channel channel, DataType type, const uint8_t* data, size_t len)
    {
        std::lock_guard<std::mutex> lock(_send_mutex);
        if (channel == Channel::Local) {
            _local_client->send(static_cast<uint8_t>(type), data, len);
        } else {
            sendRemotePacket(type, data, len);
        }
    }

    void sendPacket(DataType type, const uint8_t* data, size_t len)
    {
        std::lock_guard<std::mutex> lock(_send_mutex);
        if (_local_client->send(static_cast<uint8_t>(type), data, len)) return;
        sendRemotePacket(type, data, len);
    }
};

class WebsocketAvatarWorker : public mooncake::BasicAbility {
public:
    WebsocketAvatarWorker()
    {
        _service = std::make_unique<WebSocketAvatar>();
        _service->init();
    }

    void onCreate() override
    {
    }

    void onRunning() override
    {
        if (GetHAL().millis() - _last_tick < 20) {
            return;
        }
        _last_tick = GetHAL().millis();
        _service->update();
    }

    void onDestroy() override
    {
        _service.reset();
    }

private:
    std::unique_ptr<WebSocketAvatar> _service;
    uint32_t _last_tick = 0;
};

void Hal::startWebSocketAvatarService(std::function<void(std::string_view)> onStartLog)
{
    mclog::tagInfo(_tag, "start websocket avatar service");

    startNetwork(onStartLog);

    onStartLog("Connecting to\nserver...");
    mooncake::GetMooncake().extensionManager()->createAbility(std::make_unique<WebsocketAvatarWorker>());
}
