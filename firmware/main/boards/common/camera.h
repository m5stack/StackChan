#pragma once

#include <string>

class Camera {
public:
    virtual ~Camera() = default;

    virtual bool Capture() = 0;
    virtual bool SetHMirror(bool enabled) = 0;
    virtual bool SetVFlip(bool enabled) = 0;
    virtual std::string Explain(const std::string& question) = 0;
    virtual void SetExplainUrl(const std::string& url, const std::string& token) = 0;

    virtual bool SetSwapBytes(bool enabled)
    {
        (void)enabled;
        return false;
    }
};
