#pragma once

#include "mcp/property.h"

#include <functional>
#include <string>

class McpTool {
public:
    McpTool(const std::string& name, const std::string& description, const PropertyList& properties,
            std::function<ReturnValue(const PropertyList&)> callback);

    void set_user_only(bool user_only) { user_only_ = user_only; }
    const std::string& name() const { return name_; }
    const std::string& description() const { return description_; }
    const PropertyList& properties() const { return properties_; }
    bool user_only() const { return user_only_; }

    JsonPtr ToJsonObject() const;
    std::string to_json() const;
    std::string Call(const PropertyList& properties);

private:
    std::string name_;
    std::string description_;
    PropertyList properties_;
    std::function<ReturnValue(const PropertyList&)> callback_;
    bool user_only_ = false;
};
