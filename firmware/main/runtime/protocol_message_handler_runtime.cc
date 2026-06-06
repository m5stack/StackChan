#include <application.h>

#include <assets/lang_config.h>
#include <display.h>
#include <mcp/server.h>

#include <cJSON.h>
#include <esp_log.h>

#include <cstring>

#define TAG "Application"

void Application::HandleIncomingProtocolJson(const cJSON* root, Display* display)
{
    const cJSON* type = cJSON_GetObjectItem(root, "type");
    if (!cJSON_IsString(type)) {
        ESP_LOGW(TAG, "Unknown voice message without type");
        return;
    }

    if (HandleIncomingTtsStart(root, display) || HandleIncomingTtsSentence(root, display) ||
        HandleIncomingTtsStop(root, display) || HandleIncomingSttTranscript(root, display) ||
        HandleIncomingUiEmotion(root, display) || HandleIncomingUiAlert(root) ||
        HandleIncomingSystemReboot(root) || HandleIncomingMcp(root) ||
        HandleIncomingUiCustom(root, display)) {
        return;
    }

    ESP_LOGW(TAG, "Unknown voice message type: %s", type->valuestring);
}

bool Application::HandleIncomingTtsStart(const cJSON* root, Display* display)
{
    const cJSON* type = cJSON_GetObjectItem(root, "type");
    if (!cJSON_IsString(type) || std::strcmp(type->valuestring, "tts.start") != 0) {
        return false;
    }
    (void)display;
    Schedule([this]() {
        aborted_ = false;
        SetDeviceState(kDeviceStateSpeaking);
    });
    return true;
}

bool Application::HandleIncomingTtsSentence(const cJSON* root, Display* display)
{
    const cJSON* type = cJSON_GetObjectItem(root, "type");
    if (!cJSON_IsString(type) || std::strcmp(type->valuestring, "tts.sentence") != 0) {
        return false;
    }

    const cJSON* text = cJSON_GetObjectItem(root, "text");
    if (cJSON_IsString(text)) {
        ESP_LOGI(TAG, "<< %s", text->valuestring);
        Schedule([display, message = std::string(text->valuestring)]() {
            display->SetChatMessage("assistant", message.c_str());
        });
    }
    return true;
}

bool Application::HandleIncomingTtsStop(const cJSON* root, Display* display)
{
    const cJSON* type = cJSON_GetObjectItem(root, "type");
    if (!cJSON_IsString(type) || std::strcmp(type->valuestring, "tts.stop") != 0) {
        return false;
    }
    (void)display;
    Schedule([this]() {
        if (GetDeviceState() == kDeviceStateSpeaking) {
            if (listening_mode_ == kListeningModeManualStop) {
                SetDeviceState(kDeviceStateIdle);
            } else {
                SetDeviceState(kDeviceStateListening);
            }
        }
    });
    return true;
}

bool Application::HandleIncomingSttTranscript(const cJSON* root, Display* display)
{
    const cJSON* type = cJSON_GetObjectItem(root, "type");
    if (!cJSON_IsString(type) || std::strcmp(type->valuestring, "stt.transcript") != 0) {
        return false;
    }

    const cJSON* text = cJSON_GetObjectItem(root, "text");
    if (cJSON_IsString(text)) {
        ESP_LOGI(TAG, ">> %s", text->valuestring);
        Schedule([display, message = std::string(text->valuestring)]() {
            display->SetChatMessage("user", message.c_str());
        });
    }
    return true;
}

bool Application::HandleIncomingUiEmotion(const cJSON* root, Display* display)
{
    const cJSON* type = cJSON_GetObjectItem(root, "type");
    if (!cJSON_IsString(type) || std::strcmp(type->valuestring, "ui.emotion") != 0) {
        return false;
    }
    const cJSON* emotion = cJSON_GetObjectItem(root, "emotion");
    if (cJSON_IsString(emotion)) {
        Schedule([display, emotion_str = std::string(emotion->valuestring)]() {
            display->SetEmotion(emotion_str.c_str());
        });
    }
    return true;
}

bool Application::HandleIncomingMcp(const cJSON* root)
{
    const cJSON* type = cJSON_GetObjectItem(root, "type");
    if (!cJSON_IsString(type) || std::strcmp(type->valuestring, "mcp.message") != 0) {
        return false;
    }

    const cJSON* payload = cJSON_GetObjectItem(root, "payload");
    if (cJSON_IsObject(payload)) {
        McpServer::GetInstance().ParseMessage(payload);
    }
    return true;
}

bool Application::HandleIncomingSystemReboot(const cJSON* root)
{
    const cJSON* type = cJSON_GetObjectItem(root, "type");
    if (!cJSON_IsString(type) || std::strcmp(type->valuestring, "system.reboot") != 0) {
        return false;
    }
    Schedule([this]() { Reboot(); });
    return true;
}

bool Application::HandleIncomingUiAlert(const cJSON* root)
{
    const cJSON* type = cJSON_GetObjectItem(root, "type");
    if (!cJSON_IsString(type) || std::strcmp(type->valuestring, "ui.alert") != 0) {
        return false;
    }

    const cJSON* status = cJSON_GetObjectItem(root, "status");
    const cJSON* message = cJSON_GetObjectItem(root, "message");
    const cJSON* emotion = cJSON_GetObjectItem(root, "emotion");
    if (cJSON_IsString(status) && cJSON_IsString(message) && cJSON_IsString(emotion)) {
        Alert(status->valuestring, message->valuestring, emotion->valuestring, Lang::Sounds::OGG_VIBRATION);
    }
    return true;
}

bool Application::HandleIncomingUiCustom(const cJSON* root, Display* display)
{
#if CONFIG_RECEIVE_CUSTOM_MESSAGE
    const cJSON* type = cJSON_GetObjectItem(root, "type");
    if (!cJSON_IsString(type) || std::strcmp(type->valuestring, "ui.custom") != 0) {
        return false;
    }

    const cJSON* payload = cJSON_GetObjectItem(root, "payload");
    if (cJSON_IsObject(payload)) {
        char* printed = cJSON_PrintUnformatted(payload);
        std::string payload_str = printed != nullptr ? printed : "{}";
        if (printed != nullptr) {
            cJSON_free(printed);
        }
        Schedule([display, payload_str]() { display->SetChatMessage("system", payload_str.c_str()); });
    }
    return true;
#else
    (void)root;
    (void)display;
    return false;
#endif
}
