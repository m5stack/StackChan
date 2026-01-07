#ifndef __SI12T_H__
#define __SI12T_H__

#include <stdint.h>
#include <stdbool.h>
#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SI12T_VERSION "0.0.2"

/*LTR-507 SEL pin is "GND"*/
#define SI12T_GND_ADDRESS 0x68  // 7bit i2c address

#define SI12T_SENSITIVITY1_ADDR 0x02
#define SI12T_SENSITIVITY2_ADDR 0x03
#define SI12T_SENSITIVITY3_ADDR 0x04
#define SI12T_SENSITIVITY4_ADDR 0x05
#define SI12T_SENSITIVITY5_ADDR 0x06
#define SI12T_SENSITIVITY6_ADDR 0x07
#define SI12T_CTRL1_ADDR        0x08
#define SI12T_CTRL2_ADDR        0x09
#define SI12T_REF_RST1_ADDR     0x0A
#define SI12T_REF_RST2_ADDR     0x0B
#define SI12T_CH_HOLD1_ADDR     0x0C
#define SI12T_CH_HOLD2_ADDR     0x0D
#define SI12T_CAL_HOLD1_ADDR    0x0E
#define SI12T_CAL_HOLD2_ADDR    0x0F
#define SI12T_OUTPUT1_ADDR      0x10
#define SI12T_OUTPUT2_ADDR      0x11
#define SI12T_OUTPUT3_ADDR      0x12

typedef enum { SI12T_TYPE_LOW = 0, SI12T_TYPE_HIGH } si12t_type_t;

typedef enum {
    SI12T_SENSITIVITY_LEVEL_0 = 0,
    SI12T_SENSITIVITY_LEVEL_1,
    SI12T_SENSITIVITY_LEVEL_2,
    SI12T_SENSITIVITY_LEVEL_3,
    SI12T_SENSITIVITY_LEVEL_4,
    SI12T_SENSITIVITY_LEVEL_5,
    SI12T_SENSITIVITY_LEVEL_6,
    SI12T_SENSITIVITY_LEVEL_7,
    SI12T_SENSITIVITY_LEVEL_INVALID,
} si12t_sensitivity_level_t;

typedef enum { SI12T_OUTPUT_NONE = 0, SI12T_OUTPUT_LOW, SI12T_OUTPUT_MID, SI12T_OUTPUT_HIGH } si12t_output_t;

/**
 * @brief Si12T配置结构体
 */
typedef struct {
    i2c_master_bus_handle_t i2c_bus; /*!< I2C总线句柄 */
    uint8_t dev_addr;                /*!< 设备地址，默认SI12T_GND_ADDRESS */
} si12t_config_t;

/**
 * @brief Si12T设备句柄
 */
typedef struct si12t_dev_t *si12t_handle_t;

extern uint8_t si12t_point_type[3];

/**
 * @brief 初始化Si12T设备
 *
 * @param config 配置参数
 * @param handle 返回的设备句柄
 * @return esp_err_t ESP_OK成功，其他失败
 */
esp_err_t si12t_init(const si12t_config_t *config, si12t_handle_t *handle);

/**
 * @brief 初始化Si12T设备（使用灵敏度参数）
 *
 * @param handle 设备句柄
 * @param sens_type 灵敏度类型
 * @param sens_level 灵敏度等级
 * @return esp_err_t ESP_OK成功，其他失败
 */
esp_err_t si12t_setup(si12t_handle_t handle, si12t_type_t sens_type, si12t_sensitivity_level_t sens_level);

/**
 * @brief 删除Si12T设备
 *
 * @param handle 设备句柄
 * @return esp_err_t ESP_OK成功，其他失败
 */
esp_err_t si12t_delete(si12t_handle_t handle);

/**
 * @brief 使能所有通道
 *
 * @param handle 设备句柄
 * @return esp_err_t ESP_OK成功，其他失败
 */
esp_err_t si12t_enable_channel(si12t_handle_t handle);

/**
 * @brief 设置灵敏度
 *
 * @param handle 设备句柄
 * @param sens_type 灵敏度类型
 * @param sens_level 灵敏度等级
 * @return esp_err_t ESP_OK成功，其他失败
 */
esp_err_t si12t_set_sensitivity(si12t_handle_t handle, si12t_type_t sens_type, si12t_sensitivity_level_t sens_level);

/**
 * @brief 获取灵敏度
 *
 * @param handle 设备句柄
 * @return esp_err_t ESP_OK成功，其他失败
 */
esp_err_t si12t_get_sensitivity(si12t_handle_t handle);

/**
 * @brief 设置Ctrl1寄存器
 *
 * @param handle 设备句柄
 * @return esp_err_t ESP_OK成功，其他失败
 */
esp_err_t si12t_set_ctrl1(si12t_handle_t handle);

/**
 * @brief 设置Ctrl2寄存器
 *
 * @param handle 设备句柄
 * @return esp_err_t ESP_OK成功，其他失败
 */
esp_err_t si12t_set_ctrl2(si12t_handle_t handle);

/**
 * @brief 使能睡眠模式
 *
 * @param handle 设备句柄
 * @return esp_err_t ESP_OK成功，其他失败
 */
esp_err_t si12t_sleep_enable(si12t_handle_t handle);

/**
 * @brief 禁用睡眠模式
 *
 * @param handle 设备句柄
 * @return esp_err_t ESP_OK成功，其他失败
 */
esp_err_t si12t_sleep_disable(si12t_handle_t handle);

/**
 * @brief 读取触摸结果
 *
 * @param handle 设备句柄
 * @param touch_result 触摸结果输出
 * @return esp_err_t ESP_OK成功，其他失败
 */
esp_err_t si12t_read_touch_result(si12t_handle_t handle, uint8_t *touch_result);

/**
 * @brief 解析触摸结果
 *
 * @param touch_result 触摸结果
 */
void si12t_parse_touch_result(uint8_t touch_result);

void si12t_parse_touch_result_to(uint8_t touch_result, uint8_t *parsed_result);

#ifdef __cplusplus
}
#endif

#endif /* __SI12T_H__ */
