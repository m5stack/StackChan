#include "M5Unified.h"

extern "C" {
#include <stdio.h>
#include "esp_log.h"
#include "string.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "lvgl.h"

#include "ui.h"
#include "esp_now_init.h"
#include "joystick_handle.h"

#include "lvgl_port.h"

using namespace m5;

joystick_data_t joystick_data;

// extern void lvgl_port_init(M5GFX &gfx);

/**
 * @brief Handle Button Press.
 * 1. Press BtnA to switch setup_mode UI and running_mode UI.
 * 2. Press BtnB to switch espnow-channel or id on setup_mode;
 * 3. Press BtnB to send btnB_status to remote on running_mode.
 */
void handle_button_press()
{
    static uint8_t screen_mode = MODE_SETUP;
    // check if BtnA is pressed
    if (M5.BtnA.wasPressed()) {
        // use BtnA to switch mode
        screen_mode = (screen_mode + 1) % 3;

        if (screen_mode == MODE_SETUP) {
            // in setup mode, press A to enter running mode
            joystick_data.screen_mode = MODE_SETUP;
            switch_screen(joystick_data.screen_mode);
        } else if (screen_mode == MODE_RUNNING) {
            // in running mode, press A to enter IMU mode
            wifi_espnow_reinit(joystick_data.channel);
            joystick_data.screen_mode = MODE_RUNNING;
            switch_screen(joystick_data.screen_mode);
        } else if (screen_mode == MODE_IMU) {
            // in IMU mode, press A to return to setup mode
            joystick_data.screen_mode = MODE_IMU;
            switch_screen(joystick_data.screen_mode);
        }
    }
    if (M5.BtnB.wasPressed()) {
        if (joystick_data.screen_mode == MODE_SETUP) {
            joystick_data.select_mode = !joystick_data.select_mode;
        } else if ((joystick_data.screen_mode == MODE_RUNNING) || (joystick_data.screen_mode == MODE_IMU)) {
            joystick_data.btnB_status = !joystick_data.btnB_status;
        }
    }
}

void app_main(void)
{
    imu_data_t imu_data;

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }

    M5.begin();
    M5.Power.begin();
    M5.Power.Axp192.setLDO2(2800);  // set LDO2 to 2.8V for external devices
    M5.Imu.init(&M5.In_I2C);        // init IMU with internal I2C port
    printf("IN_I2C port: %d\n", M5.In_I2C.getPort());
    printf("M5 Display width: %ld, height: %ld\n", M5.Display.width(), M5.Display.height());

    joystick_data = joystick_init();  // init joystick

    lvgl_port_init();  // init LVGL
    ui_init();         // init UI

    // init WiFi and ESP-NOW
    wifi_espnow_init(joystick_data.channel);

    xTaskCreate(handle_setup_screen, "handle_setup_screen", 8192, &joystick_data, 5, NULL);      // handle setup mode
    xTaskCreate(handle_running_screen, "handle_running_screen", 8192, &joystick_data, 5, NULL);  // handle running mode
    xTaskCreate(handle_imu_screen, "handle_imu_screen", 8192, &joystick_data, 5, NULL);

    while (1) {
        M5.update();
        // Handle button press
        handle_button_press();
        joystick_data.bat = (M5.Power.Axp192.getBatteryLevel());  // updata battery level

        joystick_data.bat = (joystick_data.bat > 100) ? 100 : joystick_data.bat;
        joystick_data.bat = (joystick_data.bat < 0) ? 0 : joystick_data.bat;

        M5.Imu.update();                              // update IMU data
        imu_data              = M5.Imu.getImuData();  // get IMU data
        joystick_data.accel_x = imu_data.accel.x;
        joystick_data.accel_y = imu_data.accel.y;
        joystick_data.accel_z = imu_data.accel.z;

#if 0
        printf("Accel: (%.2f, %.2f, %.2f), Gyro: (%.2f, %.2f, %.2f)\n",
               joystick_data.accel_x, joystick_data.accel_y, joystick_data.accel_z,
               joystick_data.gyro_x, joystick_data.gyro_y, joystick_data.gyro_z);
#endif
        vTaskDelay(20 / portTICK_PERIOD_MS);
    }
}
}