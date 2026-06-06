#include <protocol.h>

#include <cJSON.h>
#include <esp_log.h>

#define TAG "Protocol"

namespace {

std::string PrintJson(cJSON* root)
{
    char* raw = cJSON_PrintUnformatted(root);
    if (raw == nullptr) {
        return {};
    }

    std::string result(raw);
    cJSON_free(raw);
    return result;
}

void AddSessionId(cJSON* root, const std::string& session_id)
{
    if (!session_id.empty()) {
        cJSON_AddStringToObject(root, "session_id", session_id.c_str());
    }
}

}  // namespace

void Protocol::OnIncomingJson(std::function<void(const cJSON* root)> callback)
{
    on_incoming_json_ = std::move(callback);
}

void Protocol::OnIncomingAudio(std::function<void(std::unique_ptr<AudioStreamPacket> packet)> callback)
{
    on_incoming_audio_ = std::move(callback);
}

void Protocol::OnAudioChannelOpened(std::function<void()> callback)
{
    on_audio_channel_opened_ = std::move(callback);
}

void Protocol::OnAudioChannelClosed(std::function<void()> callback)
{
    on_audio_channel_closed_ = std::move(callback);
}

void Protocol::OnNetworkError(std::function<void(const std::string& message)> callback)
{
    on_network_error_ = std::move(callback);
}

void Protocol::OnConnected(std::function<void()> callback)
{
    on_connected_ = std::move(callback);
}

void Protocol::OnDisconnected(std::function<void()> callback)
{
    on_disconnected_ = std::move(callback);
}

void Protocol::SetError(const std::string& message)
{
    error_occurred_ = true;
    if (on_network_error_ != nullptr) {
        on_network_error_(message);
    }
}

void Protocol::SendAbortSpeaking(AbortReason reason)
{
    cJSON* root = cJSON_CreateObject();
    if (root == nullptr) {
        SetError("Failed to allocate abort payload");
        return;
    }
    AddSessionId(root, session_id_);
    cJSON_AddStringToObject(root, "type", "abort");
    if (reason == kAbortReasonWakeWordDetected) {
        cJSON_AddStringToObject(root, "reason", "wake_word_detected");
    }
    const std::string message = PrintJson(root);
    cJSON_Delete(root);
    if (message.empty()) {
        SetError("Failed to serialize abort payload");
        return;
    }
    SendText(message);
}

void Protocol::SendWakeWordDetected(const std::string& wake_word)
{
    cJSON* root = cJSON_CreateObject();
    if (root == nullptr) {
        SetError("Failed to allocate wake-word payload");
        return;
    }
    AddSessionId(root, session_id_);
    cJSON_AddStringToObject(root, "type", "listen.detect");
    cJSON_AddStringToObject(root, "text", wake_word.c_str());
    const std::string message = PrintJson(root);
    cJSON_Delete(root);
    if (message.empty()) {
        SetError("Failed to serialize wake-word payload");
        return;
    }
    SendText(message);
}

void Protocol::SendStartListening(ListeningMode mode)
{
    cJSON* root = cJSON_CreateObject();
    if (root == nullptr) {
        SetError("Failed to allocate listen-start payload");
        return;
    }
    AddSessionId(root, session_id_);
    cJSON_AddStringToObject(root, "type", "listen.start");
    if (mode == kListeningModeRemoteWake) {
        cJSON_AddStringToObject(root, "mode", "remote_wake");
    } else if (mode == kListeningModeRealtime) {
        cJSON_AddStringToObject(root, "mode", "realtime");
    } else if (mode == kListeningModeAutoStop) {
        cJSON_AddStringToObject(root, "mode", "auto");
    } else {
        cJSON_AddStringToObject(root, "mode", "manual");
    }
    const std::string message = PrintJson(root);
    cJSON_Delete(root);
    if (message.empty()) {
        SetError("Failed to serialize listen-start payload");
        return;
    }
    SendText(message);
}

void Protocol::SendStopListening()
{
    cJSON* root = cJSON_CreateObject();
    if (root == nullptr) {
        SetError("Failed to allocate listen-stop payload");
        return;
    }
    AddSessionId(root, session_id_);
    cJSON_AddStringToObject(root, "type", "listen.stop");
    const std::string message = PrintJson(root);
    cJSON_Delete(root);
    if (message.empty()) {
        SetError("Failed to serialize listen-stop payload");
        return;
    }
    SendText(message);
}

void Protocol::SendMcpMessage(const std::string& payload)
{
    cJSON* payload_json = cJSON_Parse(payload.c_str());
    if (payload_json == nullptr) {
        ESP_LOGE(TAG, "Invalid MCP payload JSON");
        SetError("Invalid MCP payload JSON");
        return;
    }

    cJSON* root = cJSON_CreateObject();
    if (root == nullptr) {
        cJSON_Delete(payload_json);
        SetError("Failed to allocate MCP payload");
        return;
    }

    AddSessionId(root, session_id_);
    cJSON_AddStringToObject(root, "type", "mcp.message");
    cJSON_AddItemToObject(root, "payload", payload_json);
    const std::string message = PrintJson(root);
    cJSON_Delete(root);
    if (message.empty()) {
        SetError("Failed to serialize MCP payload");
        return;
    }
    SendText(message);
}

bool Protocol::IsTimeout() const
{
    constexpr int kTimeoutSeconds = 120;
    const auto now = std::chrono::steady_clock::now();
    const auto duration = std::chrono::duration_cast<std::chrono::seconds>(now - last_incoming_time_);
    const bool timeout = duration.count() > kTimeoutSeconds;
    if (timeout) {
        ESP_LOGE(TAG, "Channel timeout %ld seconds", static_cast<long>(duration.count()));
    }
    return timeout;
}
