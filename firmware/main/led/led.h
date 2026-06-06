#pragma once

class Led {
public:
    virtual ~Led() = default;
    virtual void OnStateChanged() = 0;
};

class NullLed final : public Led {
public:
    void OnStateChanged() override {}
};
