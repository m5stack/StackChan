#pragma once

#include <cJSON.h>

#include <memory>
#include <string>
#include <variant>

class ImageContent {
public:
    ImageContent(const std::string& mime_type, const std::string& data);

    std::string to_json() const;

private:
    static std::string Base64Encode(const std::string& data);

    std::string encoded_data_;
    std::string mime_type_;
};

using JsonPtr = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>;
using ImageContentPtr = std::unique_ptr<ImageContent>;
using ReturnValue = std::variant<bool, int, std::string, JsonPtr, ImageContentPtr>;

enum PropertyType {
    kPropertyTypeBoolean,
    kPropertyTypeInteger,
    kPropertyTypeString
};

namespace mcp::json {

std::string PrintOrThrow(cJSON* json);
JsonPtr Wrap(cJSON* json);
JsonPtr CreateObjectPtrOrThrow();
JsonPtr CreateArrayPtrOrThrow();
cJSON* CreateObjectOrThrow();
cJSON* CreateArrayOrThrow();
cJSON* CreateStringOrThrow(const char* value);
cJSON* CreateNumberOrThrow(double value);
cJSON* CreateBoolOrThrow(bool value);
JsonPtr ParseOrThrow(const std::string& text, const char* error_message);
void AddItemToObjectOrThrow(cJSON* object, const char* key, cJSON* item);
void AddItemToObjectOrThrow(cJSON* object, const char* key, JsonPtr&& item);
void AddItemToArrayOrThrow(cJSON* array, cJSON* item);
void AddItemToArrayOrThrow(cJSON* array, JsonPtr&& item);
void AddStringToObjectOrThrow(cJSON* object, const char* key, const char* value);
void AddNumberToObjectOrThrow(cJSON* object, const char* key, double value);
void AddBoolToObjectOrThrow(cJSON* object, const char* key, bool value);

}  // namespace mcp::json
