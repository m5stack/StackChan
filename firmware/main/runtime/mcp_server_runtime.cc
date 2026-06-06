#include <mcp/server.h>

#include <application.h>
#include <assets.h>
#include <audio_codec.h>
#include <board.h>
#include <display.h>
#include <firmware_identity.h>
#include <settings.h>
#include <system_info.h>

#include <esp_app_desc.h>
#include <esp_log.h>

#include <lvgl_theme.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <utility>

#define TAG "MCP"

namespace {

constexpr size_t kMaxToolsListPayloadSize = 8000;

std::string PrintJson(cJSON* json)
{
    char* raw = cJSON_PrintUnformatted(json);
    if (raw == nullptr) {
        return "{}";
    }

    std::string result(raw);
    cJSON_free(raw);
    return result;
}

std::string BuildRpcEnvelope(int id, cJSON* result)
{
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "jsonrpc", "2.0");
    cJSON_AddNumberToObject(root, "id", id);
    cJSON_AddItemToObject(root, "result", result);
    const std::string payload = PrintJson(root);
    cJSON_Delete(root);
    return payload;
}

std::string BuildRpcErrorEnvelope(int id, const std::string& message)
{
    cJSON* root  = cJSON_CreateObject();
    cJSON* error = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "jsonrpc", "2.0");
    cJSON_AddNumberToObject(root, "id", id);
    cJSON_AddStringToObject(error, "message", message.c_str());
    cJSON_AddItemToObject(root, "error", error);
    const std::string payload = PrintJson(root);
    cJSON_Delete(root);
    return payload;
}

std::string BuildInitializeResult()
{
    const esp_app_desc_t* app_desc = esp_app_get_description();

    cJSON* result      = cJSON_CreateObject();
    cJSON* caps        = cJSON_CreateObject();
    cJSON* tools       = cJSON_CreateObject();
    cJSON* server_info = cJSON_CreateObject();
    cJSON_AddStringToObject(result, "protocolVersion", "2024-11-05");
    cJSON_AddItemToObject(result, "capabilities", caps);
    cJSON_AddItemToObject(caps, "tools", tools);
    cJSON_AddItemToObject(result, "serverInfo", server_info);
    cJSON_AddStringToObject(server_info, "name", BOARD_NAME);
    cJSON_AddStringToObject(server_info, "version", app_desc->version);
    cJSON_AddStringToObject(server_info, "fork", firmware_identity::kFork);

    const std::string payload = PrintJson(result);
    cJSON_Delete(result);
    return payload;
}

}  // namespace

McpServer::McpServer() = default;

McpServer::~McpServer()
{
    for (McpTool* tool : tools_) {
        delete tool;
    }
    tools_.clear();
}

void McpServer::AddCommonTools()
{
    auto original_tools = std::move(tools_);
    auto& board         = Board::GetInstance();

    AddTool("self.get_device_status",
            "Provides real-time device information including audio, display, battery, and network status.",
            PropertyList(), [&board](const PropertyList&) -> ReturnValue { return board.GetDeviceStatusJson(); });

    AddTool("self.audio_speaker.set_volume", "Set the speaker output volume between 0 and 100.",
            PropertyList({Property("volume", kPropertyTypeInteger, 0, 100)}),
            [&board](const PropertyList& properties) -> ReturnValue {
                board.GetAudioCodec()->SetOutputVolume(properties["volume"].value<int>());
                return true;
            });

    if (Backlight* backlight = board.GetBacklight(); backlight != nullptr) {
        AddTool("self.screen.set_brightness", "Set the display brightness between 0 and 100.",
                PropertyList({Property("brightness", kPropertyTypeInteger, 0, 100)}),
                [backlight](const PropertyList& properties) -> ReturnValue {
                    backlight->SetBrightness(static_cast<uint8_t>(properties["brightness"].value<int>()), true);
                    return true;
                });
    }

    if (Display* display = board.GetDisplay(); display != nullptr && display->GetTheme() != nullptr) {
        AddTool("self.screen.set_theme", "Set the screen theme. Supported values depend on the firmware theme set.",
                PropertyList({Property("theme", kPropertyTypeString)}),
                [display](const PropertyList& properties) -> ReturnValue {
                    LvglTheme* theme =
                        LvglThemeManager::GetInstance().GetTheme(properties["theme"].value<std::string>());
                    if (theme == nullptr) {
                        throw std::runtime_error("Unknown theme");
                    }

                    display->SetTheme(theme);
                    return true;
                });
    }

    if (Camera* camera = board.GetCamera(); camera != nullptr) {
        AddTool("self.camera.take_photo",
                "Capture a photo and return analysis content. Use this when the user asks the device to look at "
                "something.",
                PropertyList({Property("question", kPropertyTypeString)}),
                [camera](const PropertyList& properties) -> ReturnValue {
                    if (!camera->Capture()) {
                        throw std::runtime_error("Failed to capture photo");
                    }

                    return camera->Explain(properties["question"].value<std::string>());
                });
    }

    tools_.insert(tools_.end(), original_tools.begin(), original_tools.end());
}

void McpServer::AddUserOnlyTools()
{
    AddUserOnlyTool("self.get_system_info", "Get low-level system information about the device.", PropertyList(),
                    [](const PropertyList&) -> ReturnValue { return Board::GetInstance().GetSystemInfoJson(); });

    AddUserOnlyTool("self.reboot", "Reboot the device.", PropertyList(), [](const PropertyList&) -> ReturnValue {
        auto& app = Application::GetInstance();
        app.Schedule([&app]() {
            ESP_LOGW(TAG, "User requested reboot");
            vTaskDelay(pdMS_TO_TICKS(500));
            app.Reboot();
        });
        return true;
    });

    AddUserOnlyTool("self.upgrade_firmware", "Download and install firmware from a URL, then reboot.",
                    PropertyList({Property("url", kPropertyTypeString)}),
                    [](const PropertyList& properties) -> ReturnValue {
                        auto url = properties["url"].value<std::string>();
                        auto& app = Application::GetInstance();
                        app.Schedule([url = std::move(url), &app]() {
                            if (!app.UpgradeFirmware(url)) {
                                ESP_LOGE(TAG, "Firmware upgrade failed: %s", url.c_str());
                            }
                        });
                        return true;
                    });

    AddUserOnlyTool("self.assets.set_download_url", "Set the assets download URL used by the firmware.",
                    PropertyList({Property("url", kPropertyTypeString)}),
                    [](const PropertyList& properties) -> ReturnValue {
                        Settings settings("assets", true);
                        settings.SetString("download_url", properties["url"].value<std::string>());
                        return true;
                    });
}

void McpServer::AddTool(McpTool* tool)
{
    auto existing = std::find_if(tools_.begin(), tools_.end(),
                                 [tool](const McpTool* current) { return current->name() == tool->name(); });
    if (existing != tools_.end()) {
        ESP_LOGW(TAG, "Tool %s already added", tool->name().c_str());
        delete tool;
        return;
    }

    ESP_LOGI(TAG, "Add tool: %s%s", tool->name().c_str(), tool->user_only() ? " [user]" : "");
    tools_.push_back(tool);
}

void McpServer::AddTool(const std::string& name, const std::string& description, const PropertyList& properties,
                        std::function<ReturnValue(const PropertyList&)> callback)
{
    AddTool(new McpTool(name, description, properties, std::move(callback)));
}

void McpServer::AddUserOnlyTool(const std::string& name, const std::string& description, const PropertyList& properties,
                                std::function<ReturnValue(const PropertyList&)> callback)
{
    auto* tool = new McpTool(name, description, properties, std::move(callback));
    tool->set_user_only(true);
    AddTool(tool);
}

void McpServer::ParseMessage(const std::string& message)
{
    cJSON* json = cJSON_Parse(message.c_str());
    if (json == nullptr) {
        ESP_LOGE(TAG, "Failed to parse MCP message");
        return;
    }

    ParseMessage(json, nullptr);
    cJSON_Delete(json);
}

void McpServer::ParseCapabilities(const cJSON* capabilities)
{
    if (capabilities == nullptr) {
        return;
    }

    const cJSON* vision = cJSON_GetObjectItemCaseSensitive(capabilities, "vision");
    if (!cJSON_IsObject(vision)) {
        return;
    }

    const cJSON* url   = cJSON_GetObjectItemCaseSensitive(vision, "url");
    const cJSON* token = cJSON_GetObjectItemCaseSensitive(vision, "token");
    if (!cJSON_IsString(url)) {
        return;
    }

    Camera* camera = Board::GetInstance().GetCamera();
    if (camera == nullptr) {
        return;
    }

    camera->SetExplainUrl(url->valuestring, cJSON_IsString(token) ? token->valuestring : "");
}

void McpServer::ParseMessage(const cJSON* json)
{
    ParseMessage(json, nullptr);
}

void McpServer::ParseMessage(const cJSON* json, ReplyHandler reply_handler)
{
    const cJSON* version = cJSON_GetObjectItemCaseSensitive(json, "jsonrpc");
    if (!cJSON_IsString(version) || std::strcmp(version->valuestring, "2.0") != 0) {
        ESP_LOGE(TAG, "Invalid JSON-RPC version");
        return;
    }

    const cJSON* method = cJSON_GetObjectItemCaseSensitive(json, "method");
    if (!cJSON_IsString(method)) {
        ESP_LOGE(TAG, "Missing method");
        return;
    }

    const std::string method_name(method->valuestring);
    if (method_name.rfind("notifications", 0) == 0) {
        return;
    }

    const cJSON* params = cJSON_GetObjectItemCaseSensitive(json, "params");
    if (params != nullptr && !cJSON_IsObject(params)) {
        ESP_LOGE(TAG, "Invalid params for method %s", method_name.c_str());
        return;
    }

    const cJSON* id = cJSON_GetObjectItemCaseSensitive(json, "id");
    if (!cJSON_IsNumber(id)) {
        ESP_LOGE(TAG, "Invalid id for method %s", method_name.c_str());
        return;
    }

    if (method_name == "initialize") {
        if (cJSON_IsObject(params)) {
            ParseCapabilities(cJSON_GetObjectItemCaseSensitive(params, "capabilities"));
        }
        ReplyResult(id->valueint, BuildInitializeResult(), reply_handler);
        return;
    }

    if (method_name == "tools/list") {
        std::string cursor;
        bool with_user_tools = false;
        if (cJSON_IsObject(params)) {
            const cJSON* cursor_item = cJSON_GetObjectItemCaseSensitive(params, "cursor");
            if (cJSON_IsString(cursor_item)) {
                cursor = cursor_item->valuestring;
            }

            const cJSON* user_tools_item = cJSON_GetObjectItemCaseSensitive(params, "withUserTools");
            if (cJSON_IsBool(user_tools_item)) {
                with_user_tools = cJSON_IsTrue(user_tools_item);
            }
        }
        GetToolsList(id->valueint, cursor, with_user_tools, reply_handler);
        return;
    }

    if (method_name == "tools/call") {
        if (!cJSON_IsObject(params)) {
            ReplyError(id->valueint, "Missing params", reply_handler);
            return;
        }

        const cJSON* name = cJSON_GetObjectItemCaseSensitive(params, "name");
        if (!cJSON_IsString(name)) {
            ReplyError(id->valueint, "Missing name", reply_handler);
            return;
        }

        const cJSON* arguments = cJSON_GetObjectItemCaseSensitive(params, "arguments");
        if (arguments != nullptr && !cJSON_IsObject(arguments)) {
            ReplyError(id->valueint, "Invalid arguments", reply_handler);
            return;
        }

        DoToolCall(id->valueint, name->valuestring, arguments, std::move(reply_handler));
        return;
    }

    ReplyError(id->valueint, "Method not implemented: " + method_name, reply_handler);
}

void McpServer::ReplyResult(int id, const std::string& result, const ReplyHandler& reply_handler)
{
    cJSON* json = cJSON_Parse(result.c_str());
    if (json == nullptr) {
        ReplyError(id, "Invalid MCP result payload", reply_handler);
        return;
    }

    const std::string payload = BuildRpcEnvelope(id, json);
    if (reply_handler) {
        reply_handler(payload);
        return;
    }

    Application::GetInstance().SendMcpMessage(payload);
}

void McpServer::ReplyError(int id, const std::string& message, const ReplyHandler& reply_handler)
{
    const std::string payload = BuildRpcErrorEnvelope(id, message);
    if (reply_handler) {
        reply_handler(payload);
        return;
    }

    Application::GetInstance().SendMcpMessage(payload);
}

void McpServer::GetToolsList(int id, const std::string& cursor, bool list_user_only_tools, const ReplyHandler& reply_handler)
{
    cJSON* result = cJSON_CreateObject();
    cJSON* tools  = cJSON_CreateArray();
    cJSON_AddItemToObject(result, "tools", tools);

    bool start_after_cursor = cursor.empty();
    std::string next_cursor;
    size_t payload_size = 16;

    for (McpTool* tool : tools_) {
        if (!start_after_cursor) {
            if (tool->name() == cursor) {
                start_after_cursor = true;
            }
            continue;
        }

        if (!list_user_only_tools && tool->user_only()) {
            continue;
        }

        try {
            auto tool_item = tool->ToJsonObject();
            const std::string tool_json = mcp::json::PrintOrThrow(tool_item.get());
            if (payload_size + tool_json.size() + 64 > kMaxToolsListPayloadSize) {
                next_cursor = tool->name();
                break;
            }

            mcp::json::AddItemToArrayOrThrow(tools, std::move(tool_item));
            payload_size += tool_json.size();
        } catch (const std::exception&) {
            cJSON_Delete(result);
            ReplyError(id, "Failed to serialize tool list", reply_handler);
            return;
        }
    }

    if (!next_cursor.empty()) {
        cJSON_AddStringToObject(result, "nextCursor", next_cursor.c_str());
    }

    const std::string payload = PrintJson(result);
    cJSON_Delete(result);
    ReplyResult(id, payload, reply_handler);
}

void McpServer::DoToolCall(int id, const std::string& tool_name, const cJSON* tool_arguments, ReplyHandler reply_handler)
{
    auto tool_iter = std::find_if(tools_.begin(), tools_.end(),
                                  [&tool_name](const McpTool* tool) { return tool->name() == tool_name; });
    if (tool_iter == tools_.end()) {
        ReplyError(id, "Unknown tool: " + tool_name, reply_handler);
        return;
    }

    McpTool* tool = *tool_iter;
    PropertyList arguments = tool->properties();
    try {
        for (Property& argument : arguments) {
            bool found = false;
            if (cJSON_IsObject(tool_arguments)) {
                const cJSON* value = cJSON_GetObjectItemCaseSensitive(tool_arguments, argument.name().c_str());
                if (argument.type() == kPropertyTypeBoolean && cJSON_IsBool(value)) {
                    argument.set_value<bool>(cJSON_IsTrue(value));
                    found = true;
                } else if (argument.type() == kPropertyTypeInteger && cJSON_IsNumber(value)) {
                    argument.set_value<int>(value->valueint);
                    found = true;
                } else if (argument.type() == kPropertyTypeString && cJSON_IsString(value)) {
                    argument.set_value<std::string>(value->valuestring);
                    found = true;
                }
            }

            if (!argument.has_default_value() && !found) {
                ReplyError(id, "Missing valid argument: " + argument.name(), reply_handler);
                return;
            }
        }
    } catch (const std::exception& e) {
        ReplyError(id, e.what(), reply_handler);
        return;
    }

    auto& app = Application::GetInstance();
    app.Schedule([this, id, tool, arguments = std::move(arguments), reply_handler = std::move(reply_handler)]() mutable {
        try {
            ReplyResult(id, tool->Call(arguments), reply_handler);
        } catch (const std::exception& e) {
            ReplyError(id, e.what(), reply_handler);
        }
    });
}
