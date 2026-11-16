/* SPDX-License-Identifier: MIT */
#pragma once
#include <memory>
#include <string>

namespace network_diagnostics {

// Serialize Wi-Fi driver handover with a diagnostic task starting saved Wi-Fi.
void prepareWifiProvisioning();
void finishWifiProvisioning();

enum class Status { Waiting, Checking, Available, Reachable, Failed };

struct ServiceResult {
    Status status      = Status::Waiting;
    std::string detail = "Waiting";
    int http_status    = 0;
    int elapsed_ms     = 0;
};

struct Snapshot {
    bool running     = false;
    std::string wifi = "Checking Wi-Fi...";
    ServiceResult xiaozhi;
    ServiceResult m5stack;
};

// Tasks retain their own state, never UI objects. Closing the page cancels
// subsequent work without waiting for DNS/TLS or accessing destroyed widgets.
class NetworkDiagnostics {
public:
    NetworkDiagnostics();
    ~NetworkDiagnostics();
    bool start();
    Snapshot snapshot() const;

private:
    struct State;
    std::shared_ptr<State> _state;
    static void run(const std::shared_ptr<State>& state);
};

}  // namespace network_diagnostics
