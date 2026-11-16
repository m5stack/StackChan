/*
 * SPDX-License-Identifier: MIT
 *
 * The robot's own MCP tools (hal_mcp.cpp) served to LAN agents such as ESP-Claw as JSON-RPC over
 * HTTP POST: initialize, ping, tools/list (paged by cursor) and tools/call. The same McpTool objects
 * are registered with xiaozhi, so the voice agent and LAN agents see identical tools.
 */
#include "hal.h"
#include "hal_lan_server.h"
#include <mcp_server.h>
#include <mooncake_log.h>
#include <cJSON.h>
#include <algorithm>
#include <memory>
#include <string>
#include <vector>

static std::vector<McpTool*> _tools;

void lan_mcp_add_tool(McpTool* tool)
{
    _tools.push_back(tool);
}

#if CONFIG_STACKCHAN_LAN_MCP_ENABLE

static const std::string_view _tag = "HAL-LAN-MCP";

static constexpr size_t _max_request_size = 8 * 1024;
static constexpr size_t _max_list_size    = 6 * 1024;  // ESP-Claw reads responses into an 8 KB buffer

static cJSON* make_initialize_result(const cJSON* params)
{
    auto requested = cJSON_GetObjectItem(params, "protocolVersion");

    cJSON* result = cJSON_CreateObject();
    cJSON_AddStringToObject(result, "protocolVersion",
                            cJSON_IsString(requested) ? requested->valuestring : "2024-11-05");
    cJSON_AddItemToObject(cJSON_AddObjectToObject(result, "capabilities"), "tools", cJSON_CreateObject());
    cJSON* info = cJSON_AddObjectToObject(result, "serverInfo");
    cJSON_AddStringToObject(info, "name", "StackChan");
    cJSON_AddStringToObject(info, "version", FIRMWARE_VERSION);
    return result;
}

// Same paging as xiaozhi: nextCursor is the name of the first tool that didn't fit
static cJSON* make_tools_list_result(const cJSON* params)
{
    auto cursor_json   = cJSON_GetObjectItem(params, "cursor");
    std::string cursor = cJSON_IsString(cursor_json) ? cursor_json->valuestring : "";

    cJSON* result = cJSON_CreateObject();
    cJSON* tools  = cJSON_AddArrayToObject(result, "tools");
    size_t size   = 0;
    bool started  = cursor.empty();
    for (auto* tool : _tools) {
        if (!started && tool->name() != cursor) {
            continue;
        }
        started = true;

        auto json = tool->to_json();
        if (size + json.size() > _max_list_size && cJSON_GetArraySize(tools) > 0) {
            cJSON_AddStringToObject(result, "nextCursor", tool->name().c_str());
            break;
        }
        size += json.size();
        cJSON_AddItemToArray(tools, cJSON_Parse(json.c_str()));
    }
    return result;
}

// Mirrors McpServer::DoToolCall's argument handling, but runs the tool on the calling task
static cJSON* call_tool(const cJSON* params, std::string& error)
{
    auto name = cJSON_GetObjectItem(params, "name");
    if (!cJSON_IsString(name)) {
        error = "Missing tool name";
        return nullptr;
    }
    auto tool = std::find_if(_tools.begin(), _tools.end(),
                             [name](const McpTool* t) { return t->name() == name->valuestring; });
    if (tool == _tools.end()) {
        error = std::string("Unknown tool: ") + name->valuestring;
        return nullptr;
    }

    auto tool_arguments    = cJSON_GetObjectItem(params, "arguments");
    PropertyList arguments = (*tool)->properties();
    try {
        for (auto& argument : arguments) {
            auto value = cJSON_IsObject(tool_arguments)
                             ? cJSON_GetObjectItem(tool_arguments, argument.name().c_str())
                             : nullptr;
            bool found = true;
            if (argument.type() == kPropertyTypeBoolean && cJSON_IsBool(value)) {
                argument.set_value<bool>(cJSON_IsTrue(value));
            } else if (argument.type() == kPropertyTypeInteger && cJSON_IsNumber(value)) {
                argument.set_value<int>(value->valueint);
            } else if (argument.type() == kPropertyTypeString && cJSON_IsString(value)) {
                argument.set_value<std::string>(value->valuestring);
            } else {
                found = false;
            }
            if (!found && !argument.has_default_value()) {
                error = "Missing valid argument: " + argument.name();
                return nullptr;
            }
        }

        mclog::tagInfo(_tag, "tools/call: {}", (*tool)->name());
        return cJSON_Parse((*tool)->Call(arguments).c_str());
    } catch (const std::exception& e) {
        error = e.what();
        return nullptr;
    }
}

static esp_err_t send_json(httpd_req_t* req, cJSON* response)
{
    char* body = cJSON_PrintUnformatted(response);
    cJSON_Delete(response);
    httpd_resp_set_type(req, "application/json");
    esp_err_t err = httpd_resp_sendstr(req, body ? body : "{}");
    cJSON_free(body);
    return err;
}

static esp_err_t send_rpc_error(httpd_req_t* req, const cJSON* id, int code, const std::string& message)
{
    cJSON* response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "jsonrpc", "2.0");
    cJSON_AddItemToObject(response, "id", id ? cJSON_Duplicate(id, true) : cJSON_CreateNull());
    cJSON* error = cJSON_AddObjectToObject(response, "error");
    cJSON_AddNumberToObject(error, "code", code);
    cJSON_AddStringToObject(error, "message", message.c_str());
    return send_json(req, response);
}

static esp_err_t mcp_handler(httpd_req_t* req)
{
    size_t len = req->content_len;
    if (len == 0 || len > _max_request_size) {
        return send_rpc_error(req, nullptr, -32600, "Request must be 1 byte to 8 KB");
    }

    std::string body(len, '\0');
    size_t received = 0;
    while (received < len) {
        int ret = httpd_req_recv(req, body.data() + received, len - received);
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (ret <= 0) {
            return ESP_FAIL;
        }
        received += ret;
    }

    std::unique_ptr<cJSON, decltype(&cJSON_Delete)> request(cJSON_Parse(body.c_str()), cJSON_Delete);
    auto method = cJSON_GetObjectItem(request.get(), "method");
    if (!request || !cJSON_IsString(method)) {
        return send_rpc_error(req, nullptr, -32700, "Parse error");
    }

    // Notifications (no id) get no JSON-RPC response
    auto id = cJSON_GetObjectItem(request.get(), "id");
    if (!id) {
        httpd_resp_set_status(req, "202 Accepted");
        return httpd_resp_send(req, nullptr, 0);
    }

    std::string name = method->valuestring;
    auto params      = cJSON_GetObjectItem(request.get(), "params");
    cJSON* result    = nullptr;
    std::string error;

    if (name == "initialize") {
        result = make_initialize_result(params);
    } else if (name == "ping") {
        result = cJSON_CreateObject();
    } else if (name == "tools/list") {
        result = make_tools_list_result(params);
    } else if (name == "tools/call") {
        result = call_tool(params, error);
    } else {
        return send_rpc_error(req, id, -32601, "Method not found: " + name);
    }
    if (!result) {
        mclog::tagWarn(_tag, "{}: {}", name, error);
        return send_rpc_error(req, id, -32602, error);
    }

    cJSON* response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "jsonrpc", "2.0");
    cJSON_AddItemToObject(response, "id", cJSON_Duplicate(id, true));
    cJSON_AddItemToObject(response, "result", result);
    return send_json(req, response);
}

void lan_mcp_register_uris(httpd_handle_t server)
{
    static const std::string uri = "/" CONFIG_STACKCHAN_LAN_MCP_ENDPOINT;
    httpd_uri_t mcp_uri          = {.uri = uri.c_str(), .method = HTTP_POST, .handler = mcp_handler, .user_ctx = nullptr};
    httpd_register_uri_handler(server, &mcp_uri);
    mclog::tagInfo(_tag, "serving {} tools at {}", _tools.size(), uri);
}

#endif
