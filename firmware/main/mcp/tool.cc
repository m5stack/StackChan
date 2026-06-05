#include "mcp/tool.h"

#include <stdexcept>

McpTool::McpTool(const std::string& name, const std::string& description, const PropertyList& properties,
                 std::function<ReturnValue(const PropertyList&)> callback)
    : name_(name), description_(description), properties_(properties), callback_(std::move(callback))
{
}

JsonPtr McpTool::ToJsonObject() const
{
    const std::vector<std::string> required = properties_.GetRequired();

    auto json = mcp::json::CreateObjectPtrOrThrow();
    mcp::json::AddStringToObjectOrThrow(json.get(), "name", name_.c_str());
    mcp::json::AddStringToObjectOrThrow(json.get(), "description", description_.c_str());

    auto input_schema = mcp::json::CreateObjectPtrOrThrow();
    mcp::json::AddStringToObjectOrThrow(input_schema.get(), "type", "object");

    auto properties = properties_.ToJsonObject();
    mcp::json::AddItemToObjectOrThrow(input_schema.get(), "properties", std::move(properties));

    if (!required.empty()) {
        auto required_array = mcp::json::CreateArrayPtrOrThrow();
        for (const auto& property : required) {
            mcp::json::AddItemToArrayOrThrow(required_array.get(), mcp::json::CreateStringOrThrow(property.c_str()));
        }
        mcp::json::AddItemToObjectOrThrow(input_schema.get(), "required", std::move(required_array));
    }

    mcp::json::AddItemToObjectOrThrow(json.get(), "inputSchema", std::move(input_schema));

    if (user_only_) {
        auto annotations = mcp::json::CreateObjectPtrOrThrow();
        auto audience = mcp::json::CreateArrayPtrOrThrow();
        mcp::json::AddItemToArrayOrThrow(audience.get(), mcp::json::CreateStringOrThrow("user"));
        mcp::json::AddItemToObjectOrThrow(annotations.get(), "audience", std::move(audience));
        mcp::json::AddItemToObjectOrThrow(json.get(), "annotations", std::move(annotations));
    }

    return json;
}

std::string McpTool::to_json() const
{
    auto json = ToJsonObject();
    return mcp::json::PrintOrThrow(json.get());
}

std::string McpTool::Call(const PropertyList& properties)
{
    ReturnValue return_value = callback_(properties);
    auto result = mcp::json::CreateObjectPtrOrThrow();
    auto content = mcp::json::CreateArrayPtrOrThrow();

    if (std::holds_alternative<ImageContentPtr>(return_value)) {
        auto& image_content = std::get<ImageContentPtr>(return_value);
        auto image = mcp::json::CreateObjectPtrOrThrow();
        mcp::json::AddStringToObjectOrThrow(image.get(), "type", "image");
        mcp::json::AddStringToObjectOrThrow(image.get(), "image", image_content->to_json().c_str());
        mcp::json::AddItemToArrayOrThrow(content.get(), std::move(image));
    } else {
        auto text = mcp::json::CreateObjectPtrOrThrow();
        mcp::json::AddStringToObjectOrThrow(text.get(), "type", "text");
        if (std::holds_alternative<std::string>(return_value)) {
            mcp::json::AddStringToObjectOrThrow(text.get(), "text", std::get<std::string>(return_value).c_str());
        } else if (std::holds_alternative<bool>(return_value)) {
            mcp::json::AddStringToObjectOrThrow(text.get(), "text", std::get<bool>(return_value) ? "true" : "false");
        } else if (std::holds_alternative<int>(return_value)) {
            mcp::json::AddStringToObjectOrThrow(text.get(), "text", std::to_string(std::get<int>(return_value)).c_str());
        } else if (std::holds_alternative<JsonPtr>(return_value)) {
            auto& json_value = std::get<JsonPtr>(return_value);
            const std::string json_text = mcp::json::PrintOrThrow(json_value.get());
            mcp::json::AddStringToObjectOrThrow(text.get(), "text", json_text.c_str());
        }
        mcp::json::AddItemToArrayOrThrow(content.get(), std::move(text));
    }

    mcp::json::AddItemToObjectOrThrow(result.get(), "content", std::move(content));
    mcp::json::AddBoolToObjectOrThrow(result.get(), "isError", false);
    const std::string result_str = mcp::json::PrintOrThrow(result.get());
    return result_str;
}
