/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <cstdint>
#include "driver/i2c_master.h"
#include "esp_err.h"
#include "BMI270_SensorAPI/bmi270.h"

struct BMI270_Data {
    float accel_x;
    float accel_y;
    float accel_z;
    float gyro_x;
    float gyro_y;
    float gyro_z;
};

class BMI270 {
public:
    static constexpr uint8_t DEFAULT_ADDRESS = BMI2_I2C_PRIM_ADDR;  // 0x68

    BMI270(i2c_master_bus_handle_t i2c_bus_handle, uint8_t addr = DEFAULT_ADDRESS);
    ~BMI270();

    /**
     * @brief Initialize the device
     *
     * @return true if successful
     * @return false if failed
     */
    bool begin();

    /**
     * @brief Read sensor data
     *
     * @return true if successful
     * @return false if failed
     */
    bool update();

    void getAccelerometer(float& x, float& y, float& z);
    void getGyroscope(float& x, float& y, float& z);
    const BMI270_Data& getData();

private:
    i2c_master_dev_handle_t _i2c_dev;
    struct bmi2_dev _bmi;
    uint8_t _addr;
    bool _initialized;
    BMI270_Data _data;

    static BMI2_INTF_RETURN_TYPE bmi2_i2c_read(uint8_t reg_addr, uint8_t* reg_data, uint32_t len, void* intf_ptr);
    static BMI2_INTF_RETURN_TYPE bmi2_i2c_write(uint8_t reg_addr, const uint8_t* reg_data, uint32_t len,
                                                void* intf_ptr);
    static void bmi2_delay_us(uint32_t period, void* intf_ptr);

    float lsb_to_mps2(int16_t val, float g_range, uint8_t bit_width);
    float lsb_to_dps(int16_t val, float dps, uint8_t bit_width);
};
