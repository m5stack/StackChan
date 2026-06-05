#pragma once

#include <mcp/server.h>
#include <settings.h>

class PressToTalkMcpTool {
public:
    PressToTalkMcpTool();

    void Initialize();
    bool IsPressToTalkEnabled() const;

private:
    ReturnValue HandleSetPressToTalk(const PropertyList& properties);
    void SetPressToTalkEnabled(bool enabled);

    bool press_to_talk_enabled_;
};
