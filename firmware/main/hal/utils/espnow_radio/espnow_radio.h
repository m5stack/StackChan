#pragma once

#include <esp_err.h>

// ESP-NOW uses an exclusive fixed-channel radio until the app's existing reboot
// on exit. It needs no IP netif or additional Wi-Fi event subscriptions.
class EspNowRadio {
public:
    esp_err_t Start(int channel);
    void Stop();

private:
    bool owns_driver_ = false;
    bool started_     = false;
};
