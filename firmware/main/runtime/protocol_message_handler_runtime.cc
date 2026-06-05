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
        ESP_LOGW(TAG, "Unknown message without type");
        return;
    }

    if (HandleIncomingTtsMessage(root, display) || HandleIncomingSttMessage(root, display) ||
        HandleIncomingLlmMessage(root, display) || HandleIncomingMcpMessage(root) ||
        HandleIncomingSystemMessage(root) || HandleIncomingAlertMessage(root) ||
        HandleIncomingCustomMessage(root, display)) {
        return;
    }

    ESP_LOGW(TAG, "Unknown message type: %s", type->valuestring);
}

bool Application::HandleIncomingTtsMessage(const cJSON* root, Display* display)
{
    const cJSON* type = cJSON_GetObjectItem(root, "type");
    if (!cJSON_IsString(type) || std::strcmp(type->valuestring, "tts") != 0) {
        return false;
    }

    const cJSON* state = cJSON_GetObjectItem(root, "state");
    if (cJSON_IsString(state) && std::strcmp(state->valuestring, "start") == 0) {
        Schedule([this]() {
            aborted_ = false;
            SetDeviceState(kDeviceStateSpeaking);
        });
        return true;
    }

    if (cJSON_IsString(state) && std::strcmp(state->valuestring, "stop") == 0) {
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

    if (cJSON_IsString(state) && std::strcmp(state->valuestring, "sentence_start") == 0) {
        const cJSON* text = cJSON_GetObjectItem(root, "text");
        if (cJSON_IsString(text)) {
            ESP_LOGI(TAG, "<< %s", text->valuestring);
            Schedule([display, message = std::string(text->valuestring)]() {
                display->SetChatMessage("assistant", message.c_str());
            });
        }
    }

    return true;
}

bool Application::HandleIncomingSttMessage(const cJSON* root, Display* display)
{
    const cJSON* type = cJSON_GetObjectItem(root, "type");
    if (!cJSON_IsString(type) || std::strcmp(type->valuestring, "stt") != 0) {
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

bool Application::HandleIncomingLlmMessage(const cJSON* root, Display* display)
{
    const cJSON* type = cJSON_GetObjectItem(root, "type");
    if (!cJSON_IsString(type) || std::strcmp(type->valuestring, "llm") != 0) {
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

bool Application::HandleIncomingMcpMessage(const cJSON* root)
{
    const cJSON* type = cJSON_GetObjectItem(root, "type");
    if (!cJSON_IsString(type) || std::strcmp(type->valuestring, "mcp") != 0) {
        return false;
    }

    const cJSON* payload = cJSON_GetObjectItem(root, "payload");
    if (cJSON_IsObject(payload)) {
        McpServer::GetInstance().ParseMessage(payload);
    }
    return true;
}

bool Application::HandleIncomingSystemMessage(const cJSON* root)
{
    const cJSON* type = cJSON_GetObjectItem(root, "type");
    if (!cJSON_IsString(type) || std::strcmp(type->valuestring, "system") != 0) {
        return false;
    }

    const cJSON* command = cJSON_GetObjectItem(root, "command");
    if (cJSON_IsString(command) && std::strcmp(command->valuestring, "reboot") == 0) {
        Schedule([this]() { Reboot(); });
    }
    return true;
}

bool Application::HandleIncomingAlertMessage(const cJSON* root)
{
    const cJSON* type = cJSON_GetObjectItem(root, "type");
    if (!cJSON_IsString(type) || std::strcmp(type->valuestring, "alert") != 0) {
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

bool Application::HandleIncomingCustomMessage(const cJSON* root, Display* display)
{
#if CONFIG_RECEIVE_CUSTOM_MESSAGE
    const cJSON* type = cJSON_GetObjectItem(root, "type");
    if (!cJSON_IsString(type) || std::strcmp(type->valuestring, "custom") != 0) {
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
