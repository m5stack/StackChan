#include "mcp/property.h"

#include <stdexcept>

Property::Property(const std::string& name, PropertyType type, int min_value, int max_value)
    : name_(name), type_(type), has_default_value_(false), min_value_(min_value), max_value_(max_value)
{
    if (type != kPropertyTypeInteger) {
        throw std::invalid_argument("Range limits only apply to integer properties");
    }
}

Property::Property(const std::string& name, PropertyType type, int default_value, int min_value, int max_value)
    : name_(name), type_(type), value_(default_value), has_default_value_(true), min_value_(min_value),
      max_value_(max_value)
{
    if (type != kPropertyTypeInteger) {
        throw std::invalid_argument("Range limits only apply to integer properties");
    }
    if (default_value < min_value || default_value > max_value) {
        throw std::invalid_argument("Default value must be within the specified range");
    }
}

JsonPtr Property::ToJsonObject() const
{
    auto json = mcp::json::CreateObjectPtrOrThrow();

    if (type_ == kPropertyTypeBoolean) {
        mcp::json::AddStringToObjectOrThrow(json.get(), "type", "boolean");
        if (has_default_value_) {
            mcp::json::AddBoolToObjectOrThrow(json.get(), "default", value<bool>());
        }
    } else if (type_ == kPropertyTypeInteger) {
        mcp::json::AddStringToObjectOrThrow(json.get(), "type", "integer");
        if (has_default_value_) {
            mcp::json::AddNumberToObjectOrThrow(json.get(), "default", value<int>());
        }
        if (min_value_.has_value()) {
            mcp::json::AddNumberToObjectOrThrow(json.get(), "minimum", min_value_.value());
        }
        if (max_value_.has_value()) {
            mcp::json::AddNumberToObjectOrThrow(json.get(), "maximum", max_value_.value());
        }
    } else if (type_ == kPropertyTypeString) {
        mcp::json::AddStringToObjectOrThrow(json.get(), "type", "string");
        if (has_default_value_) {
            mcp::json::AddStringToObjectOrThrow(json.get(), "default", value<std::string>().c_str());
        }
    }

    return json;
}

std::string Property::to_json() const
{
    auto json = ToJsonObject();
    return mcp::json::PrintOrThrow(json.get());
}

void PropertyList::AddProperty(const Property& property)
{
    properties_.push_back(property);
}

Property& PropertyList::operator[](const std::string& name)
{
    for (auto& property : properties_) {
        if (property.name() == name) {
            return property;
        }
    }
    throw std::runtime_error("Property not found: " + name);
}

const Property& PropertyList::operator[](const std::string& name) const
{
    for (const auto& property : properties_) {
        if (property.name() == name) {
            return property;
        }
    }
    throw std::runtime_error("Property not found: " + name);
}

std::vector<std::string> PropertyList::GetRequired() const
{
    std::vector<std::string> required;
    for (const auto& property : properties_) {
        if (!property.has_default_value()) {
            required.push_back(property.name());
        }
    }
    return required;
}

JsonPtr PropertyList::ToJsonObject() const
{
    auto json = mcp::json::CreateObjectPtrOrThrow();

    for (const auto& property : properties_) {
        auto prop_json = property.ToJsonObject();
        mcp::json::AddItemToObjectOrThrow(json.get(), property.name().c_str(), std::move(prop_json));
    }

    return json;
}

std::string PropertyList::to_json() const
{
    auto json = ToJsonObject();
    return mcp::json::PrintOrThrow(json.get());
}
