#pragma once

#include <driver/gpio.h>

class SystemReset {
public:
    SystemReset(gpio_num_t reset_nvs_pin, gpio_num_t reset_factory_pin);

    void CheckButtons();

private:
    void ResetNvsFlash();
    void ResetToFactory();
    void RestartInSeconds(int seconds);

    gpio_num_t reset_nvs_pin_;
    gpio_num_t reset_factory_pin_;
};
