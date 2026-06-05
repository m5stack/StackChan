#include <device_state_machine.h>

#include <algorithm>

#include <esp_log.h>

#define TAG "StateMachine"

namespace {

const char* const kStateNames[] = {
    "unknown",
    "starting",
    "wifi_configuring",
    "idle",
    "connecting",
    "listening",
    "speaking",
    "upgrading",
    "activating",
    "audio_testing",
    "fatal_error",
    "invalid_state",
};

}  // namespace

DeviceStateMachine::DeviceStateMachine() = default;

const char* DeviceStateMachine::GetStateName(DeviceState state)
{
    if (state >= 0 && state <= kDeviceStateFatalError) {
        return kStateNames[state];
    }
    return kStateNames[kDeviceStateFatalError + 1];
}

bool DeviceStateMachine::IsValidTransition(DeviceState from, DeviceState to) const
{
    if (from == to) {
        return true;
    }

    switch (from) {
        case kDeviceStateUnknown:
            return to == kDeviceStateStarting;
        case kDeviceStateStarting:
            return to == kDeviceStateWifiConfiguring || to == kDeviceStateActivating;
        case kDeviceStateWifiConfiguring:
            return to == kDeviceStateActivating || to == kDeviceStateAudioTesting;
        case kDeviceStateAudioTesting:
            return to == kDeviceStateWifiConfiguring;
        case kDeviceStateActivating:
            return to == kDeviceStateUpgrading || to == kDeviceStateIdle || to == kDeviceStateWifiConfiguring;
        case kDeviceStateUpgrading:
            return to == kDeviceStateIdle || to == kDeviceStateActivating;
        case kDeviceStateIdle:
            return to == kDeviceStateConnecting || to == kDeviceStateListening || to == kDeviceStateSpeaking ||
                   to == kDeviceStateActivating || to == kDeviceStateUpgrading || to == kDeviceStateWifiConfiguring;
        case kDeviceStateConnecting:
            return to == kDeviceStateIdle || to == kDeviceStateListening;
        case kDeviceStateListening:
            return to == kDeviceStateSpeaking || to == kDeviceStateIdle;
        case kDeviceStateSpeaking:
            return to == kDeviceStateListening || to == kDeviceStateIdle;
        case kDeviceStateFatalError:
            return false;
        default:
            return false;
    }
}

bool DeviceStateMachine::CanTransitionTo(DeviceState target) const
{
    return IsValidTransition(current_state_.load(), target);
}

bool DeviceStateMachine::TransitionTo(DeviceState new_state)
{
    const DeviceState old_state = current_state_.load();
    if (old_state == new_state) {
        return true;
    }

    if (!IsValidTransition(old_state, new_state)) {
        ESP_LOGW(TAG, "Invalid state transition: %s -> %s", GetStateName(old_state), GetStateName(new_state));
        return false;
    }

    current_state_.store(new_state);
    ESP_LOGI(TAG, "State: %s -> %s", GetStateName(old_state), GetStateName(new_state));
    NotifyStateChange(old_state, new_state);
    return true;
}

int DeviceStateMachine::AddStateChangeListener(StateCallback callback)
{
    std::lock_guard<std::mutex> lock(mutex_);
    const int listener_id = next_listener_id_++;
    listeners_.emplace_back(listener_id, std::move(callback));
    return listener_id;
}

void DeviceStateMachine::RemoveStateChangeListener(int listener_id)
{
    std::lock_guard<std::mutex> lock(mutex_);
    listeners_.erase(std::remove_if(listeners_.begin(), listeners_.end(),
                                    [listener_id](const auto& listener) { return listener.first == listener_id; }),
                     listeners_.end());
}

void DeviceStateMachine::NotifyStateChange(DeviceState old_state, DeviceState new_state)
{
    std::vector<StateCallback> listeners_copy;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        listeners_copy.reserve(listeners_.size());
        for (const auto& listener : listeners_) {
            listeners_copy.push_back(listener.second);
        }
    }

    for (const StateCallback& listener : listeners_copy) {
        listener(old_state, new_state);
    }
}
