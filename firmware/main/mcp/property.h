#pragma once

#include "mcp/types.h"

#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

class Property {
public:
    Property(const std::string& name, PropertyType type) : name_(name), type_(type), has_default_value_(false) {}

    template <typename T>
    Property(const std::string& name, PropertyType type, const T& default_value)
        : name_(name), type_(type), value_(default_value), has_default_value_(true)
    {
    }

    Property(const std::string& name, PropertyType type, int min_value, int max_value);
    Property(const std::string& name, PropertyType type, int default_value, int min_value, int max_value);

    const std::string& name() const { return name_; }
    PropertyType type() const { return type_; }
    bool has_default_value() const { return has_default_value_; }
    bool has_range() const { return min_value_.has_value() && max_value_.has_value(); }
    int min_value() const { return min_value_.value_or(0); }
    int max_value() const { return max_value_.value_or(0); }

    template <typename T>
    T value() const
    {
        return std::get<T>(value_);
    }

    template <typename T>
    void set_value(const T& value)
    {
        if constexpr (std::is_same_v<T, int>) {
            if (min_value_.has_value() && value < min_value_.value()) {
                throw std::invalid_argument("Value is below minimum allowed: " + std::to_string(min_value_.value()));
            }
            if (max_value_.has_value() && value > max_value_.value()) {
                throw std::invalid_argument("Value exceeds maximum allowed: " + std::to_string(max_value_.value()));
            }
        }
        value_ = value;
    }

    JsonPtr ToJsonObject() const;
    std::string to_json() const;

private:
    std::string name_;
    PropertyType type_;
    std::variant<bool, int, std::string> value_;
    bool has_default_value_;
    std::optional<int> min_value_;
    std::optional<int> max_value_;
};

class PropertyList {
public:
    PropertyList() = default;
    PropertyList(const std::vector<Property>& properties) : properties_(properties) {}

    void AddProperty(const Property& property);

    Property& operator[](const std::string& name);
    const Property& operator[](const std::string& name) const;

    auto begin() { return properties_.begin(); }
    auto end() { return properties_.end(); }
    auto begin() const { return properties_.begin(); }
    auto end() const { return properties_.end(); }

    std::vector<std::string> GetRequired() const;
    JsonPtr ToJsonObject() const;
    std::string to_json() const;

private:
    std::vector<Property> properties_;
};
