#include <websocket_protocol.h>

#include <application.h>
#include <board.h>
#include <settings.h>
#include <system_info.h>
#include <assets/lang_config.h>

#include <arpa/inet.h>
#include <cJSON.h>
#include <cstring>
#include <esp_log.h>
#include <esp_random.h>
#include <mbedtls/md.h>

#include <limits>

#define TAG "WS"

static std::string BytesToHex(const uint8_t* bytes, size_t len)
{
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(len * 2);
    for (size_t i = 0; i < len; ++i) {
        out.push_back(kHex[(bytes[i] >> 4) & 0x0f]);
        out.push_back(kHex[bytes[i] & 0x0f]);
    }
    return out;
}

static std::string RandomHex(size_t byte_count)
{
    std::string out;
    out.reserve(byte_count * 2);
    for (size_t i = 0; i < byte_count; ++i) {
        const uint8_t byte = static_cast<uint8_t>(esp_random() & 0xff);
        static constexpr char kHex[] = "0123456789abcdef";
        out.push_back(kHex[(byte >> 4) & 0x0f]);
        out.push_back(kHex[byte & 0x0f]);
    }
    return out;
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

static bool DecodeBinaryProtocol2Frame(const char* data, size_t len, int sample_rate, int frame_duration,
                                       AudioStreamPacket& out)
{
    if (len < sizeof(BinaryProtocol2)) {
        ESP_LOGW(TAG, "Truncated binary v2 frame: %u", static_cast<unsigned>(len));
        return false;
    }

    BinaryProtocol2 header = {};
    std::memcpy(&header, data, sizeof(header));
    const uint16_t version = ntohs(header.version);
    const uint16_t type = ntohs(header.type);
    const uint32_t timestamp = ntohl(header.timestamp);
    const uint32_t payload_size = ntohl(header.payload_size);
    if (version != 2) {
        ESP_LOGW(TAG, "Unexpected binary v2 version: %u", version);
        return false;
    }
    if (type != 0) {
        ESP_LOGW(TAG, "Unsupported binary v2 type: %u", type);
        return false;
    }
    if (payload_size > len - sizeof(BinaryProtocol2)) {
        ESP_LOGW(TAG, "Invalid binary v2 payload size %u for frame length %u", payload_size, static_cast<unsigned>(len));
        return false;
    }

    const uint8_t* payload = reinterpret_cast<const uint8_t*>(data) + sizeof(BinaryProtocol2);
    out = AudioStreamPacket{
        .sample_rate = sample_rate,
        .frame_duration = frame_duration,
        .timestamp = timestamp,
        .payload = std::vector<uint8_t>(payload, payload + payload_size),
    };
    return true;
}

static bool DecodeBinaryProtocol3Frame(const char* data, size_t len, int sample_rate, int frame_duration,
                                       AudioStreamPacket& out)
{
    if (len < sizeof(BinaryProtocol3)) {
        ESP_LOGW(TAG, "Truncated binary v3 frame: %u", static_cast<unsigned>(len));
        return false;
    }

    BinaryProtocol3 header = {};
    std::memcpy(&header, data, sizeof(header));
    if (header.type != 0) {
        ESP_LOGW(TAG, "Unsupported binary v3 type: %u", static_cast<unsigned>(header.type));
        return false;
    }

    const uint16_t payload_size = ntohs(header.payload_size);
    if (payload_size > len - sizeof(BinaryProtocol3)) {
        ESP_LOGW(TAG, "Invalid binary v3 payload size %u for frame length %u", payload_size, static_cast<unsigned>(len));
        return false;
    }

    const uint8_t* payload = reinterpret_cast<const uint8_t*>(data) + sizeof(BinaryProtocol3);
    out = AudioStreamPacket{
        .sample_rate = sample_rate,
        .frame_duration = frame_duration,
        .timestamp = 0,
        .payload = std::vector<uint8_t>(payload, payload + payload_size),
    };
    return true;
}

WebsocketProtocol::WebsocketProtocol()
{
    event_group_handle_ = xEventGroupCreate();
}

WebsocketProtocol::~WebsocketProtocol()
{
    vEventGroupDelete(event_group_handle_);
}

bool WebsocketProtocol::Start()
{
    return true;
}

bool WebsocketProtocol::SendAudio(std::unique_ptr<AudioStreamPacket> packet)
{
    if (websocket_ == nullptr || !websocket_->IsConnected() || !authenticated_ || !server_ready_) {
        return false;
    }

    if (version_ == 2) {
        if (packet->payload.size() > std::numeric_limits<uint32_t>::max()) {
            ESP_LOGE(TAG, "Audio packet too large for binary protocol v2: %u",
                     static_cast<unsigned>(packet->payload.size()));
            return false;
        }

        std::string serialized(sizeof(BinaryProtocol2) + packet->payload.size(), '\0');
        auto* bp2 = reinterpret_cast<BinaryProtocol2*>(serialized.data());
        bp2->version = htons(version_);
        bp2->type = 0;
        bp2->reserved = 0;
        bp2->timestamp = htonl(packet->timestamp);
        bp2->payload_size = htonl(packet->payload.size());
        std::memcpy(bp2->payload, packet->payload.data(), packet->payload.size());
        return websocket_->Send(serialized.data(), serialized.size(), true);
    }

    if (version_ == 3) {
        if (packet->payload.size() > std::numeric_limits<uint16_t>::max()) {
            ESP_LOGE(TAG, "Audio packet too large for binary protocol v3: %u",
                     static_cast<unsigned>(packet->payload.size()));
            return false;
        }

        std::string serialized(sizeof(BinaryProtocol3) + packet->payload.size(), '\0');
        auto* bp3 = reinterpret_cast<BinaryProtocol3*>(serialized.data());
        bp3->type = 0;
        bp3->reserved = 0;
        bp3->payload_size = htons(packet->payload.size());
        std::memcpy(bp3->payload, packet->payload.data(), packet->payload.size());
        return websocket_->Send(serialized.data(), serialized.size(), true);
    }

    return websocket_->Send(packet->payload.data(), packet->payload.size(), true);
}

bool WebsocketProtocol::SendText(const std::string& text)
{
    if (websocket_ == nullptr || !websocket_->IsConnected()) {
        return false;
    }

    if (!websocket_->Send(text)) {
        ESP_LOGE(TAG, "Failed to send text: %s", text.c_str());
        SetError(Lang::Strings::SERVER_ERROR);
        return false;
    }

    return true;
}

bool WebsocketProtocol::IsAudioChannelOpened() const
{
    return websocket_ != nullptr && websocket_->IsConnected() && authenticated_ && server_ready_ &&
           !error_occurred_ && !IsTimeout();
}

void WebsocketProtocol::CloseAudioChannel(bool send_goodbye)
{
    (void)send_goodbye;
    authenticated_ = false;
    server_ready_ = false;
    token_.clear();
    device_nonce_.clear();
    server_nonce_.clear();
    server_id_.clear();
    session_id_.clear();

    if (websocket_ != nullptr) {
        websocket_->Close();
        websocket_.reset();
    }
}

bool WebsocketProtocol::OpenAudioChannel()
{
    Settings settings("websocket", false);
    std::string url = settings.GetString("url");
    token_ = settings.GetString("token");
    const int version = settings.GetInt("version");
    if (version > 0) {
        version_ = version;
    }

    error_occurred_ = false;
    authenticated_ = false;
    server_ready_ = false;
    device_nonce_.clear();
    server_nonce_.clear();
    server_id_.clear();
    session_id_.clear();
    xEventGroupClearBits(event_group_handle_, WEBSOCKET_PROTOCOL_AUTH_EVENT | WEBSOCKET_PROTOCOL_SERVER_HELLO_EVENT);

    if (url.empty() || token_.empty()) {
        ESP_LOGE(TAG, "Missing websocket configuration");
        SetError("Missing voice websocket configuration");
        return false;
    }

    auto* network = Board::GetInstance().GetNetwork();
    websocket_ = network->CreateWebSocket(1);
    if (websocket_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create websocket");
        SetError("Failed to create voice websocket");
        return false;
    }

    websocket_->SetHeader("Protocol-Version", std::to_string(version_).c_str());
    websocket_->SetHeader("Device-Id", SystemInfo::GetMacAddress().c_str());
    websocket_->SetHeader("Client-Id", Board::GetInstance().GetUuid().c_str());

    websocket_->OnData([this](const char* data, size_t len, bool binary) {
        if (binary) {
            if (!authenticated_ || !server_ready_) {
                ESP_LOGW(TAG, "Ignoring unauthenticated binary frame");
            } else if (on_incoming_audio_ != nullptr) {
                if (version_ == 2) {
                    AudioStreamPacket packet;
                    if (DecodeBinaryProtocol2Frame(data, len, server_sample_rate_, server_frame_duration_, packet)) {
                        on_incoming_audio_(std::make_unique<AudioStreamPacket>(std::move(packet)));
                    }
                } else if (version_ == 3) {
                    AudioStreamPacket packet;
                    if (DecodeBinaryProtocol3Frame(data, len, server_sample_rate_, server_frame_duration_, packet)) {
                        on_incoming_audio_(std::make_unique<AudioStreamPacket>(std::move(packet)));
                    }
                } else {
                    on_incoming_audio_(std::make_unique<AudioStreamPacket>(AudioStreamPacket{
                        .sample_rate = server_sample_rate_,
                        .frame_duration = server_frame_duration_,
                        .timestamp = 0,
                        .payload = std::vector<uint8_t>(reinterpret_cast<const uint8_t*>(data),
                                                        reinterpret_cast<const uint8_t*>(data) + len),
                    }));
                }
            }
        } else {
            cJSON* root = cJSON_ParseWithLength(data, len);
            if (root == nullptr) {
                ESP_LOGE(TAG, "Invalid JSON frame");
            } else {
                const cJSON* type = cJSON_GetObjectItem(root, "type");
                if (cJSON_IsString(type)) {
                    if (std::strcmp(type->valuestring, "auth") == 0) {
                        ParseAuthMessage(root);
                    } else if (std::strcmp(type->valuestring, "hello") == 0) {
                        ParseServerHello(root);
                    } else if (!authenticated_ || !server_ready_) {
                        ESP_LOGW(TAG, "Ignoring unauthenticated JSON message of type %s", type->valuestring);
                    } else if (on_incoming_json_ != nullptr) {
                        on_incoming_json_(root);
                    }
                } else {
                    ESP_LOGE(TAG, "Missing message type");
                }
                cJSON_Delete(root);
            }
        }

        last_incoming_time_ = std::chrono::steady_clock::now();
    });

    websocket_->OnDisconnected([this]() {
        ESP_LOGI(TAG, "Websocket disconnected");
        authenticated_ = false;
        server_ready_ = false;
        if (on_audio_channel_closed_ != nullptr) {
            on_audio_channel_closed_();
        }
    });

    websocket_->OnConnected([this]() {
        if (on_connected_ != nullptr) {
            on_connected_();
        }
    });

    websocket_->OnError([this](int error_code) {
        ESP_LOGE(TAG, "Websocket error: %d", error_code);
        if (on_disconnected_ != nullptr) {
            on_disconnected_();
        }
    });

    ESP_LOGI(TAG, "Connecting to websocket server: %s with version: %d", url.c_str(), version_);
    if (!websocket_->Connect(url.c_str())) {
        ESP_LOGE(TAG, "Failed to connect to websocket server, code=%d", websocket_->GetLastError());
        SetError(Lang::Strings::SERVER_NOT_CONNECTED);
        websocket_.reset();
        return false;
    }

    if (!SendText(GetAuthBeginMessage())) {
        websocket_.reset();
        return false;
    }

    EventBits_t bits = xEventGroupWaitBits(event_group_handle_, WEBSOCKET_PROTOCOL_AUTH_EVENT, pdTRUE, pdFALSE,
                                           pdMS_TO_TICKS(10000));
    if (!(bits & WEBSOCKET_PROTOCOL_AUTH_EVENT) || !authenticated_) {
        ESP_LOGE(TAG, "Failed to complete websocket auth");
        SetError("Voice websocket authentication failed");
        websocket_.reset();
        return false;
    }

    if (!SendText(GetHelloMessage())) {
        websocket_.reset();
        return false;
    }

    bits = xEventGroupWaitBits(event_group_handle_, WEBSOCKET_PROTOCOL_SERVER_HELLO_EVENT, pdTRUE, pdFALSE,
                               pdMS_TO_TICKS(10000));
    if (!(bits & WEBSOCKET_PROTOCOL_SERVER_HELLO_EVENT)) {
        ESP_LOGE(TAG, "Failed to receive server hello");
        SetError(Lang::Strings::SERVER_TIMEOUT);
        websocket_.reset();
        return false;
    }

    if (on_audio_channel_opened_ != nullptr) {
        on_audio_channel_opened_();
    }

    return true;
}

std::string WebsocketProtocol::GetHelloMessage()
{
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "hello");
    cJSON_AddNumberToObject(root, "version", version_);

    cJSON* features = cJSON_CreateObject();
#if CONFIG_USE_SERVER_AEC
    cJSON_AddBoolToObject(features, "aec", true);
#endif
#if CONFIG_USE_REMOTE_WAKE_WORD
    cJSON_AddBoolToObject(features, "remote_wake", true);
#endif
    cJSON_AddBoolToObject(features, "mcp", true);
    cJSON_AddItemToObject(root, "features", features);
    cJSON_AddStringToObject(root, "transport", "websocket");

    cJSON* audio_params = cJSON_CreateObject();
    cJSON_AddStringToObject(audio_params, "format", "opus");
    cJSON_AddNumberToObject(audio_params, "sample_rate", 16000);
    cJSON_AddNumberToObject(audio_params, "channels", 1);
    cJSON_AddNumberToObject(audio_params, "frame_duration", OPUS_FRAME_DURATION_MS);
    cJSON_AddItemToObject(root, "audio_params", audio_params);

    char* json_str = cJSON_PrintUnformatted(root);
    std::string message = json_str != nullptr ? json_str : "{}";
    if (json_str != nullptr) {
        cJSON_free(json_str);
    }
    cJSON_Delete(root);
    return message;
}

std::string WebsocketProtocol::GetAuthBeginMessage()
{
    device_nonce_ = RandomHex(16);

    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "auth");
    cJSON_AddStringToObject(root, "state", "begin");
    cJSON_AddStringToObject(root, "algorithm", "hmac-sha256-v1");
    cJSON_AddStringToObject(root, "device_id", SystemInfo::GetMacAddress().c_str());
    cJSON_AddStringToObject(root, "client_id", Board::GetInstance().GetUuid().c_str());
    cJSON_AddStringToObject(root, "device_nonce", device_nonce_.c_str());
    cJSON_AddStringToObject(root, "transport", "websocket");
    cJSON_AddNumberToObject(root, "protocol_version", version_);

    char* json_str = cJSON_PrintUnformatted(root);
    std::string message = json_str != nullptr ? json_str : "{}";
    if (json_str != nullptr) {
        cJSON_free(json_str);
    }
    cJSON_Delete(root);
    return message;
}

std::string WebsocketProtocol::BuildAuthProofMessage(const char* role) const
{
    return std::string(role) + ":" + SystemInfo::GetMacAddress() + ":" + Board::GetInstance().GetUuid() + ":" +
           device_nonce_ + ":" + server_nonce_;
}

void WebsocketProtocol::ParseAuthMessage(const cJSON* root)
{
    const cJSON* state = cJSON_GetObjectItem(root, "state");
    if (!cJSON_IsString(state)) {
        ESP_LOGE(TAG, "Missing auth state");
        SetError("Invalid auth message");
        xEventGroupSetBits(event_group_handle_, WEBSOCKET_PROTOCOL_AUTH_EVENT);
        return;
    }

    if (std::strcmp(state->valuestring, "challenge") == 0) {
        const cJSON* server_nonce = cJSON_GetObjectItem(root, "server_nonce");
        const cJSON* proof = cJSON_GetObjectItem(root, "proof");
        const cJSON* server_id = cJSON_GetObjectItem(root, "server_id");
        if (!cJSON_IsString(server_nonce) || !cJSON_IsString(proof)) {
            ESP_LOGE(TAG, "Invalid auth challenge");
            SetError("Invalid auth challenge");
            xEventGroupSetBits(event_group_handle_, WEBSOCKET_PROTOCOL_AUTH_EVENT);
            return;
        }

        server_nonce_ = server_nonce->valuestring;
        server_id_ = cJSON_IsString(server_id) ? server_id->valuestring : "";

        const std::string expected_proof = HmacSha256Hex(token_, BuildAuthProofMessage("server"));
        if (expected_proof.empty() || !ConstantTimeEquals(expected_proof, proof->valuestring)) {
            ESP_LOGE(TAG, "Server auth proof verification failed");
            SetError("Server auth verification failed");
            xEventGroupSetBits(event_group_handle_, WEBSOCKET_PROTOCOL_AUTH_EVENT);
            return;
        }

        cJSON* response = cJSON_CreateObject();
        cJSON_AddStringToObject(response, "type", "auth");
        cJSON_AddStringToObject(response, "state", "verify");
        if (!server_id_.empty()) {
            cJSON_AddStringToObject(response, "server_id", server_id_.c_str());
        }

        const std::string device_proof = HmacSha256Hex(token_, BuildAuthProofMessage("device"));
        cJSON_AddStringToObject(response, "proof", device_proof.c_str());

        char* json_str = cJSON_PrintUnformatted(response);
        const std::string message = json_str != nullptr ? json_str : "{}";
        if (json_str != nullptr) {
            cJSON_free(json_str);
        }
        cJSON_Delete(response);

        if (!SendText(message)) {
            SetError("Failed to send auth verify");
            xEventGroupSetBits(event_group_handle_, WEBSOCKET_PROTOCOL_AUTH_EVENT);
        }
        return;
    }

    if (std::strcmp(state->valuestring, "ok") == 0) {
        authenticated_ = true;
        xEventGroupSetBits(event_group_handle_, WEBSOCKET_PROTOCOL_AUTH_EVENT);
        ESP_LOGI(TAG, "Voice websocket authenticated%s%s", server_id_.empty() ? "" : " with ",
                 server_id_.empty() ? "" : server_id_.c_str());
        return;
    }

    if (std::strcmp(state->valuestring, "error") == 0) {
        const cJSON* message = cJSON_GetObjectItem(root, "message");
        SetError(cJSON_IsString(message) ? message->valuestring : "Server rejected websocket auth");
        xEventGroupSetBits(event_group_handle_, WEBSOCKET_PROTOCOL_AUTH_EVENT);
        return;
    }

    ESP_LOGW(TAG, "Unknown auth state: %s", state->valuestring);
    SetError("Unknown auth state");
    xEventGroupSetBits(event_group_handle_, WEBSOCKET_PROTOCOL_AUTH_EVENT);
}

void WebsocketProtocol::ParseServerHello(const cJSON* root)
{
    if (!authenticated_) {
        ESP_LOGW(TAG, "Ignoring server hello before auth");
        return;
    }

    const cJSON* transport = cJSON_GetObjectItem(root, "transport");
    if (!cJSON_IsString(transport) || std::strcmp(transport->valuestring, "websocket") != 0) {
        ESP_LOGE(TAG, "Unsupported transport");
        return;
    }

    const cJSON* session_id = cJSON_GetObjectItem(root, "session_id");
    if (cJSON_IsString(session_id)) {
        session_id_ = session_id->valuestring;
        ESP_LOGI(TAG, "Session ID: %s", session_id_.c_str());
    }

    const cJSON* audio_params = cJSON_GetObjectItem(root, "audio_params");
    if (cJSON_IsObject(audio_params)) {
        const cJSON* sample_rate = cJSON_GetObjectItem(audio_params, "sample_rate");
        if (cJSON_IsNumber(sample_rate)) {
            server_sample_rate_ = sample_rate->valueint;
        }

        const cJSON* frame_duration = cJSON_GetObjectItem(audio_params, "frame_duration");
        if (cJSON_IsNumber(frame_duration)) {
            server_frame_duration_ = frame_duration->valueint;
        }
    }

    server_ready_ = true;
    xEventGroupSetBits(event_group_handle_, WEBSOCKET_PROTOCOL_SERVER_HELLO_EVENT);
}
