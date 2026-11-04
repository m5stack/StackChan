/* SPDX-License-Identifier: MIT */
#pragma once
#include <esp_wifi_types_generic.h>
#include <cstdint>
#include <string>

// Optional Wi-Fi hints the App may attach to a setWifi request. Every field is
// optional so older Apps that only send an SSID and password keep working.
//
// The hints never block a connection attempt: a router may broadcast one SSID
// on 2.4 GHz and 5 GHz at the same time, so the device always tries to join and
// only uses these hints to explain a failure that means "not visible here".
struct WifiBandInfo {
    // The App reported that this SSID runs on 5 GHz (e.g. it measured a
    // frequency above 4900 MHz for its own connection).
    bool reported_5ghz = false;
    // The phone currently uses this SSID. This is context only: neither the
    // phone's connection nor the device's inability to see it proves its band.
    bool current_network = false;

    bool has_5ghz_hint() const
    {
        return reported_5ghz;
    }
};

struct WifiConnectionFailure {
    std::string code;
    std::string message;
    uint16_t reason = 0;
    int sdk_error   = 0;
};

inline WifiConnectionFailure wifiNot2G4Failure(uint16_t reason = 0)
{
    return {"not_2g4",
            "StackChan only supports 2.4 GHz Wi-Fi. This network is a 5 GHz network or is out of 2.4 GHz range. "
            "Enable the router's 2.4 GHz band (or use its 2.4 GHz SSID) and try again.",
            reason};
}

inline WifiConnectionFailure wifiDisconnectFailure(uint16_t reason, const WifiBandInfo& band = {})
{
    // An explicit frequency hint can explain a 2.4 GHz miss. It does not prove
    // that the router lacks 2.4 GHz; the same SSID may be dual-band.
    if (reason == WIFI_REASON_NO_AP_FOUND && band.has_5ghz_hint()) return wifiNot2G4Failure(reason);
    switch (reason) {
        case WIFI_REASON_NO_AP_FOUND:
            return {"ap_not_found", "Wi-Fi network not found. Check the SSID, 2.4 GHz network and router range.",
                    reason};
        case WIFI_REASON_AUTH_FAIL:
        case WIFI_REASON_802_1X_AUTH_FAILED:
            return {"authentication_failed",
                    "Wi-Fi authentication failed. Check the password and router security settings.", reason};
        case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
        case WIFI_REASON_HANDSHAKE_TIMEOUT:
        case WIFI_REASON_MIC_FAILURE:
            return {"handshake_failed",
                    "Wi-Fi password verification/handshake failed. Check the password and signal strength.", reason};
        case WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY:
        case WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD:
        case WIFI_REASON_GROUP_CIPHER_INVALID:
        case WIFI_REASON_PAIRWISE_CIPHER_INVALID:
        case WIFI_REASON_AKMP_INVALID:
        case WIFI_REASON_CIPHER_SUITE_REJECTED:
            return {"unsupported_security",
                    "The router security mode is incompatible. Try WPA2-Personal on a 2.4 GHz network.", reason};
        case WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD:
        case WIFI_REASON_BEACON_TIMEOUT:
            return {"weak_signal", "Wi-Fi signal is too weak or the router stopped responding. Move the device closer.",
                    reason};
        case WIFI_REASON_ASSOC_TOOMANY:
            return {"ap_full", "The router has reached its connected-device limit.", reason};
        case WIFI_REASON_ASSOC_FAIL:
        case WIFI_REASON_DISASSOC_DUE_TO_INACTIVITY:
            return {"association_failed",
                    "The router rejected or timed out the connection. Check access restrictions and signal strength.",
                    reason};
        case WIFI_REASON_AUTH_EXPIRE:
        case WIFI_REASON_CONNECTION_FAIL:
        case WIFI_REASON_TIMEOUT:
            return {"connection_timeout",
                    "The router did not complete the connection. Check the password, signal and access restrictions.",
                    reason};
        default:
            return {"connection_failed", "Wi-Fi connection failed (driver reason " + std::to_string(reason) + ").",
                    reason};
    }
}
