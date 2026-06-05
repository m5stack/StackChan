#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <vector>

#include "device_state.h"

class DeviceStateMachine {
public:
    using StateCallback = std::function<void(DeviceState, DeviceState)>;

    DeviceStateMachine();
    ~DeviceStateMachine() = default;

    DeviceStateMachine(const DeviceStateMachine&) = delete;
    DeviceStateMachine& operator=(const DeviceStateMachine&) = delete;

    DeviceState GetState() const { return current_state_.load(); }
    bool TransitionTo(DeviceState new_state);
    bool CanTransitionTo(DeviceState target) const;
    int AddStateChangeListener(StateCallback callback);
    void RemoveStateChangeListener(int listener_id);

    static const char* GetStateName(DeviceState state);

private:
    std::atomic<DeviceState> current_state_{kDeviceStateUnknown};
    std::vector<std::pair<int, StateCallback>> listeners_;
    int next_listener_id_{0};
    std::mutex mutex_;

    bool IsValidTransition(DeviceState from, DeviceState to) const;
    void NotifyStateChange(DeviceState old_state, DeviceState new_state);
};
