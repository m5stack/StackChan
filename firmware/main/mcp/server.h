#pragma once

#include "mcp/tool.h"

#include <functional>
#include <string>
#include <vector>

class McpServer {
public:
    using ReplyHandler = std::function<void(const std::string& payload)>;

    static McpServer& GetInstance()
    {
        static McpServer instance;
        return instance;
    }

    void AddCommonTools();
    void AddUserOnlyTools();
    void AddTool(McpTool* tool);
    void AddTool(const std::string& name, const std::string& description, const PropertyList& properties,
                 std::function<ReturnValue(const PropertyList&)> callback);
    void AddUserOnlyTool(const std::string& name, const std::string& description, const PropertyList& properties,
                         std::function<ReturnValue(const PropertyList&)> callback);
    void ParseMessage(const cJSON* json);
    void ParseMessage(const cJSON* json, ReplyHandler reply_handler);
    void ParseMessage(const std::string& message);

private:
    McpServer();
    ~McpServer();

    void ParseCapabilities(const cJSON* capabilities);
    void ReplyResult(int id, const std::string& result, const ReplyHandler& reply_handler = nullptr);
    void ReplyError(int id, const std::string& message, const ReplyHandler& reply_handler = nullptr);
    void GetToolsList(int id, const std::string& cursor, bool list_user_only_tools, const ReplyHandler& reply_handler);
    void DoToolCall(int id, const std::string& tool_name, const cJSON* tool_arguments, ReplyHandler reply_handler);

    std::vector<McpTool*> tools_;
};
