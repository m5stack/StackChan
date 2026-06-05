#include "mcp/types.h"

#include <mbedtls/base64.h>

#include <stdexcept>

namespace mcp::json {

std::string PrintOrThrow(cJSON* json)
{
    char* json_str = cJSON_PrintUnformatted(json);
    if (json_str == nullptr) {
        throw std::runtime_error("Failed to serialize JSON");
    }

    std::string result(json_str);
    cJSON_free(json_str);
    return result;
}

JsonPtr Wrap(cJSON* json)
{
    return JsonPtr(json, cJSON_Delete);
}

JsonPtr CreateObjectPtrOrThrow()
{
    return Wrap(CreateObjectOrThrow());
}

JsonPtr CreateArrayPtrOrThrow()
{
    return Wrap(CreateArrayOrThrow());
}

cJSON* CreateObjectOrThrow()
{
    cJSON* item = cJSON_CreateObject();
    if (item == nullptr) {
        throw std::runtime_error("Failed to allocate JSON object");
    }
    return item;
}

cJSON* CreateArrayOrThrow()
{
    cJSON* item = cJSON_CreateArray();
    if (item == nullptr) {
        throw std::runtime_error("Failed to allocate JSON array");
    }
    return item;
}

cJSON* CreateStringOrThrow(const char* value)
{
    cJSON* item = cJSON_CreateString(value);
    if (item == nullptr) {
        throw std::runtime_error("Failed to allocate JSON string");
    }
    return item;
}

cJSON* CreateNumberOrThrow(double value)
{
    cJSON* item = cJSON_CreateNumber(value);
    if (item == nullptr) {
        throw std::runtime_error("Failed to allocate JSON number");
    }
    return item;
}

cJSON* CreateBoolOrThrow(bool value)
{
    cJSON* item = cJSON_CreateBool(value);
    if (item == nullptr) {
        throw std::runtime_error("Failed to allocate JSON boolean");
    }
    return item;
}

JsonPtr ParseOrThrow(const std::string& text, const char* error_message)
{
    cJSON* item = cJSON_Parse(text.c_str());
    if (item == nullptr) {
        throw std::runtime_error(error_message);
    }
    return Wrap(item);
}

void AddItemToObjectOrThrow(cJSON* object, const char* key, cJSON* item)
{
    if (item == nullptr) {
        throw std::runtime_error("Failed to allocate JSON child item");
    }
    cJSON_AddItemToObject(object, key, item);
}

void AddItemToObjectOrThrow(cJSON* object, const char* key, JsonPtr&& item)
{
    AddItemToObjectOrThrow(object, key, item.release());
}

void AddItemToArrayOrThrow(cJSON* array, cJSON* item)
{
    if (item == nullptr) {
        throw std::runtime_error("Failed to allocate JSON child item");
    }
    cJSON_AddItemToArray(array, item);
}

void AddItemToArrayOrThrow(cJSON* array, JsonPtr&& item)
{
    AddItemToArrayOrThrow(array, item.release());
}

void AddStringToObjectOrThrow(cJSON* object, const char* key, const char* value)
{
    AddItemToObjectOrThrow(object, key, CreateStringOrThrow(value));
}

void AddNumberToObjectOrThrow(cJSON* object, const char* key, double value)
{
    AddItemToObjectOrThrow(object, key, CreateNumberOrThrow(value));
}

void AddBoolToObjectOrThrow(cJSON* object, const char* key, bool value)
{
    AddItemToObjectOrThrow(object, key, CreateBoolOrThrow(value));
}

}  // namespace mcp::json

ImageContent::ImageContent(const std::string& mime_type, const std::string& data)
    : encoded_data_(Base64Encode(data)), mime_type_(mime_type)
{
}

std::string ImageContent::to_json() const
{
    auto json = mcp::json::CreateObjectPtrOrThrow();
    mcp::json::AddStringToObjectOrThrow(json.get(), "type", "image");
    mcp::json::AddStringToObjectOrThrow(json.get(), "mimeType", mime_type_.c_str());
    mcp::json::AddStringToObjectOrThrow(json.get(), "data", encoded_data_.c_str());
    const std::string result = mcp::json::PrintOrThrow(json.get());
    return result;
}

std::string ImageContent::Base64Encode(const std::string& data)
{
    size_t required = 0;
    size_t written = 0;
    if (mbedtls_base64_encode(nullptr, 0, &required, reinterpret_cast<const unsigned char*>(data.data()),
                              data.size()) != MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL) {
        throw std::runtime_error("Failed to size base64 buffer");
    }

    std::string result(required, '\0');
    if (mbedtls_base64_encode(reinterpret_cast<unsigned char*>(result.data()), result.size(), &written,
                              reinterpret_cast<const unsigned char*>(data.data()), data.size()) != 0) {
        throw std::runtime_error("Failed to base64 encode image content");
    }
    result.resize(written);
    return result;
}
