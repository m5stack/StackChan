/**
* Copyright (c) 2025 Bosch Sensortec GmbH. All rights reserved.
*
* BSD-3-Clause
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are met:
*
* 1. Redistributions of source code must retain the above copyright
*    notice, this list of conditions and the following disclaimer.
*
* 2. Redistributions in binary form must reproduce the above copyright
*    notice, this list of conditions and the following disclaimer in the
*    documentation and/or other materials provided with the distribution.
*
* 3. Neither the name of the copyright holder nor the names of its
*    contributors may be used to endorse or promote products derived from
*    this software without specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
* "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
* LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
* FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
* COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
* INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
* (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
* SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
* HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
* STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
* IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
* POSSIBILITY OF SUCH DAMAGE.
*
* @file       bmi270_dsd.h
* @date       2025-04-22
* @version    v2.113.0
*
*/

/**
 * \ingroup bmi2xy
 * \defgroup bmi270_dsd BMI270_DSD
 * @brief Sensor driver for BMI270_DSD sensor
 */

#ifndef BMI270_DSD_H_
#define BMI270_DSD_H_

/*! CPP guard */
#ifdef __cplusplus
extern "C" {
#endif

/***************************************************************************/

/*!             Header files
 ****************************************************************************/
#include "bmi2.h"

/***************************************************************************/

/*!               Macro definitions
 ****************************************************************************/

/*! @name BMI270_DSD chip identifier */
#define BMI270_DSD_CHIP_ID                       UINT8_C(0x24)

/*! @name BMI270_DSD feature input start addresses */
#define BMI270_DSD_CONFIG_ID_STRT_ADDR           UINT8_C(0x00)
#define BMI270_DSD_MAX_BURST_LEN_STRT_ADDR       UINT8_C(0x02)
#define BMI270_DSD_CRT_GYRO_SELF_TEST_STRT_ADDR  UINT8_C(0x03)
#define BMI270_DSD_ABORT_STRT_ADDR               UINT8_C(0x03)
#define BMI270_DSD_GYRO_SELF_OFF_STRT_ADDR       UINT8_C(0x05)
#define BMI270_DSD_NVM_PROG_PREP_STRT_ADDR       UINT8_C(0x05)
#define BMI270_DSD_GYRO_GAIN_UPDATE_STRT_ADDR    UINT8_C(0x06)
#define BMI270_DSD_DOOR_STATUS_DET_STRT_ADDR     UINT8_C(0x00)
#define BMI270_DSD_DOOR_STATUS_DET_2_STRT_ADDR   UINT8_C(0x00)
#define BMI270_DSD_ANY_MOT_STRT_ADDR             UINT8_C(0x08)
#define BMI270_DSD_NO_MOT_STRT_ADDR              UINT8_C(0x04)

/*! @name BMI270_DSD feature output start addresses */
#define BMI270_DSD_DOOR_EVENT_OUT_STRT_ADDR      UINT8_C(0x06)
#define BMI270_DSD_HEADING_OUT_STRT_ADDR         UINT8_C(0x08)
#define BMI270_DSD_GYR_USER_GAIN_OUT_STRT_ADDR   UINT8_C(0x0A)
#define BMI270_DSD_GYRO_CROSS_SENSE_STRT_ADDR    UINT8_C(0x0C)
#define BMI270_DSD_NVM_VFRM_OUT_STRT_ADDR        UINT8_C(0x0E)

/*! @name Defines maximum number of pages */
#define BMI270_DSD_MAX_PAGE_NUM                  UINT8_C(8)

/*! @name Defines maximum number of feature input configurations */
#define BMI270_DSD_MAX_FEAT_IN                   UINT8_C(11)

/*! @name Defines maximum number of feature outputs */
#define BMI270_DSD_MAX_FEAT_OUT                  UINT8_C(6)

/*! @name Mask definitions for feature interrupt mapping bits */
#define BMI270_DSD_INT_DOOR_STATE_DETECTOR_MASK  UINT8_C(0x08)
#define BMI270_DSD_INT_NO_MOT_MASK               UINT8_C(0x20)
#define BMI270_DSD_INT_ANY_MOT_MASK              UINT8_C(0x40)

/*! @name Defines maximum number of feature interrupts */
#define BMI270_DSD_MAX_INT_MAP                   UINT8_C(3)

/*! @name BMI270 door state detector output type */
#define BMI270_DSD_DOOR_EVENT_OUTPUT             UINT8_C(1)
#define BMI270_DSD_HEADING_OUTPUT                UINT8_C(2)

/*! @name Mask definitions for BMI2 door state detector feature configuration */
#define BMI270_DSD_ENABLE_MASK                   UINT8_C(0x01)
#define BMI270_DSD_REMAP_FLAG_MASK               UINT8_C(0x60)       /* 0x 1100000 */
#define BMI270_DSD_Z_SIGN_MASK                   UINT8_C(0x80)       /* 0x10000000 */
#define BMI270_DSD_Z_AXIS_MASK                   UINT16_C(0x300)
#define BMI270_DSD_GYRO_CALIB_APPLY_MASK         UINT16_C(0x400)
#define BMI270_DSD_INIT_CALIB_THR_MASK           UINT8_C(0xFF)
#define BMI270_DSD_RESET_ENABLE_FLAG_MASK        UINT16_C(0x100)
#define BMI270_DSD_BIAS_X_LOW_WORD_MASK          UINT16_C(0xFFFF)
#define BMI270_DSD_BIAS_X_HIGH_WORD_MASK         UINT16_C(0xFFFF)
#define BMI270_DSD_BIAS_Y_LOW_WORD_MASK          UINT16_C(0xFFFF)
#define BMI270_DSD_BIAS_Y_HIGH_WORD_MASK         UINT16_C(0xFFFF)
#define BMI270_DSD_BIAS_Z_LOW_WORD_MASK          UINT16_C(0xFFFF)
#define BMI270_DSD_BIAS_Z_HIGH_WORD_MASK         UINT16_C(0xFFFF)
#define BMI270_DSD_DOOR_CLOSED_THR_MASK          UINT16_C(0x3FF)
#define BMI270_DSD_INT_MASK                      UINT8_C(0x08)

/*! @name Bit position definitions for BMI2 door state detector feature configuration */
#define BMI270_DSD_REMAP_FLAG_POS                UINT8_C(0x5)       /* 0x 100000 */
#define BMI270_DSD_Z_SIGN_POS                    UINT8_C(0x7)       /* 0x10000000 */
#define BMI270_DSD_Z_AXIS_POS                    UINT16_C(0x8)
#define BMI270_DSD_GYRO_CALIB_APPLY_POS          UINT16_C(0xA)
#define BMI270_DSD_RESET_ENABLE_FLAG_POS         UINT16_C(0x8)

/*! @name Defines GPIO0 output MASK */
#define BMI270_DSD_DSD_OUT_MASK                  UINT8_C(0x03)
#define BMI270_DSD_CALIB_FLAG_MASK               UINT8_C(0x04)

/*! @name Defines GPIO0 output POS */
#define BMI270_DSD_CALIB_FLAG_POS                UINT8_C(0x02)

/*! @name Mask definitions for feature interrupt status bits */
#define BMI270_DSD_STATUS_MASK                   UINT8_C(0x08)
#define BMI270_DSD_NO_MOT_STATUS_MASK            UINT8_C(0x20)
#define BMI270_DSD_ANY_MOT_STATUS_MASK           UINT8_C(0x40)

/***************************************************************************/

/*!     BMI270_DSD User Interface function prototypes
 ****************************************************************************/

/**
 * \ingroup bmi270_dsd
 * \defgroup bmi270_dsdApiInit Initialization
 * @brief Initialize the sensor and device structure
 */

/*!
 * \ingroup bmi270_dsdApiInit
 * \page bmi270_dsd_api_bmi270_dsd_init bmi270_dsd_init
 * \code
 * int8_t bmi270_dsd_init(struct bmi2_dev *dev);
 * \endcode
 * @details This API:
 *  1) updates the device structure with address of the configuration file.
 *  2) Initializes BMI270_DSD sensor.
 *  3) Writes the configuration file.
 *  4) Updates the feature offset parameters in the device structure.
 *  5) Updates the maximum number of pages, in the device structure.
 *
 * @param[in, out] dev      : Structure instance of bmi2_dev.
 *
 * @return Result of API execution status
 * @retval 0 -> Success
 * @retval < 0 -> Fail
 */
int8_t bmi270_dsd_init(struct bmi2_dev *dev);

/**
 * \ingroup bmi270_dsd
 * \defgroup bmi270_dsdApiSensor Feature Set
 * @brief Enable / Disable features of the sensor
 */

/*!
 * \ingroup bmi270_dsdApiSensor
 * \page bmi270_dsd_api_bmi270_dsd_sensor_enable bmi270_dsd_sensor_enable
 * \code
 * int8_t bmi270_dsd_sensor_enable(const uint8_t *sens_list, uint8_t n_sens, struct bmi2_dev *dev);
 * \endcode
 * @details This API selects the sensors/features to be enabled.
 *
 * @param[in]       sens_list   : Pointer to select the sensor/feature.
 * @param[in]       n_sens      : Number of sensors selected.
 * @param[in, out]  dev         : Structure instance of bmi2_dev.
 *
 * @note Sensors/features that can be enabled.
 *
 *@verbatim
 *    sens_list                |  Values
 * ----------------------------|-----------
 * BMI2_ACCEL                  |  0
 * BMI2_GYRO                   |  1
 * BMI2_AUX                    |  2
 * BMI2_GYRO_GAIN_UPDATE       |  9
 * BMI2_DOOR_STATE_DETECTOR    |  57
 * BMI2_TEMP                   |  31
 *@endverbatim
 *
 * @note :
 * example  uint8_t sens_list[2]  = {BMI2_ACCEL, BMI2_GYRO};
 *           uint8_t n_sens        = 2;
 *
 * @return Result of API execution status
 * @retval 0 -> Success
 * @retval < 0 -> Fail
 */
int8_t bmi270_dsd_sensor_enable(const uint8_t *sens_list, uint8_t n_sens, struct bmi2_dev *dev);

/*!
 * \ingroup bmi270_dsdApiSensor
 * \page bmi270_dsd_api_bmi270_dsd_sensor_disable bmi270_dsd_sensor_disable
 * \code
 * int8_t bmi270_dsd_sensor_disable(const uint8_t *sens_list, uint8_t n_sens, struct bmi2_dev *dev);
 * \endcode
 * @details This API selects the sensors/features to be disabled.
 *
 * @param[in]       sens_list   : Pointer to select the sensor/feature.
 * @param[in]       n_sens      : Number of sensors selected.
 * @param[in, out]  dev         : Structure instance of bmi2_dev.
 *
 * @note Sensors/features that can be disabled.
 *
 *@verbatim
 *    sens_list                |  Values
 * ----------------------------|-----------
 * BMI2_ACCEL                  |  0
 * BMI2_GYRO                   |  1
 * BMI2_AUX                    |  2
 * BMI2_GYRO_GAIN_UPDATE       |  9
 * BMI2_DOOR_STATE_DETECTOR    |  57
 * BMI2_TEMP                   |  31
 *@endverbatim
 *
 * @note :
 * example  uint8_t sens_list[2]  = {BMI2_ACCEL, BMI2_GYRO};
 *           uint8_t n_sens        = 2;
 *
 * @return Result of API execution status
 * @retval 0 -> Success
 * @retval < 0 -> Fail
 */
int8_t bmi270_dsd_sensor_disable(const uint8_t *sens_list, uint8_t n_sens, struct bmi2_dev *dev);

/**
 * \ingroup bmi270_dsd
 * \defgroup bmi270_dsdApiSensorC Sensor Configuration
 * @brief Enable / Disable feature configuration of the sensor
 */

/*!
 * \ingroup bmi270_dsdApiSensorC
 * \page bmi270_dsd_api_bmi270_dsd_set_sensor_config bmi270_dsd_set_sensor_config
 * \code
 * int8_t bmi270_dsd_set_sensor_config(struct bmi2_sens_config *sens_cfg, uint8_t n_sens, struct bmi2_dev *dev);
 * \endcode
 * @details This API sets the sensor/feature configuration.
 *
 * @param[in]       sens_cfg     : Structure instance of bmi2_sens_config.
 * @param[in]       n_sens       : Number of sensors selected.
 * @param[in, out]  dev          : Structure instance of bmi2_dev.
 *
 * @note Sensors/features that can be configured
 *
 *@verbatim
 *    sens_list                |  Values
 * ----------------------------|-----------
 * BMI2_DOOR_STATE_DETECTOR    |  57
 *@endverbatim
 *
 * @return Result of API execution status
 * @retval 0 -> Success
 * @retval < 0 -> Fail
 */
int8_t bmi270_dsd_set_sensor_config(struct bmi2_sens_config *sens_cfg, uint8_t n_sens, struct bmi2_dev *dev);

/*!
 * \ingroup bmi270_dsdApiSensorC
 * \page bmi270_dsd_api_bmi270_dsd_get_sensor_config bmi270_dsd_get_sensor_config
 * \code
 * int8_t bmi270_dsd_get_sensor_config(struct bmi2_sens_config *sens_cfg, uint8_t n_sens, struct bmi2_dev *dev);
 * \endcode
 * @details This API gets the sensor/feature configuration.
 *
 * @param[in]       sens_cfg     : Structure instance of bmi2_sens_config.
 * @param[in]       n_sens       : Number of sensors selected.
 * @param[in, out]  dev          : Structure instance of bmi2_dev.
 *
 * @note Sensors/features whose configurations can be read.
 *
 *@verbatim
 *  sens_list                  |  Values
 * ----------------------------|-----------
 * BMI2_DOOR_STATE_DETECTOR    |  57
 *@endverbatim
 *
 * @return Result of API execution status
 * @retval 0 -> Success
 * @retval < 0 -> Fail
 */
int8_t bmi270_dsd_get_sensor_config(struct bmi2_sens_config *sens_cfg, uint8_t n_sens, struct bmi2_dev *dev);

/**
 * \ingroup bmi270_dsd
 * \defgroup bmi270_dsdApiSensorD Feature Sensor Data
 * @brief Get feature sensor data
 */

/*!
 * \ingroup bmi270_dsdApiSensorD
 * \page bmi270_dsd_api_bmi270_dsd_get_feature_data bmi270_dsd_get_feature_data
 * \code
 * int8_t bmi270_dsd_get_feature_data(struct bmi2_feat_sensor_data *feature_data, uint8_t n_sens, struct bmi2_dev *dev);
 * \endcode
 * @details This API gets the feature data for accelerometer, gyroscope,
 * auxiliary sensor, step counter, high-g, gyroscope user-gain update,
 * orientation, gyroscope cross sensitivity and error status for NVM and VFRM.
 *
 * @param[out] feature_data   : Structure instance of bmi2_feat_sensor_data.
 * @param[in]  n_sens         : Number of sensors selected.
 * @param[in]  dev            : Structure instance of bmi2_dev.
 *
 * @note Sensors/features whose data can be read
 *
 *@verbatim
 *  sens_list                   |  Values
 * -----------------------------|-----------
 * BMI2_DOOR_STATE_DETECTOR     |  57
 * BMI2_NVM_STATUS              |  38
 * BMI2_VFRM_STATUS             |  39
 *@endverbatim
 *
 * @return Result of API execution status
 * @retval 0 -> Success
 * @retval < 0 -> Fail
 */
int8_t bmi270_dsd_get_feature_data(struct bmi2_feat_sensor_data *feature_data, uint8_t n_sens, struct bmi2_dev *dev);

/**
 * \ingroup bmi270_dsd
 * \defgroup bmi270_dsdApiGyroUG Gyro User Gain
 * @brief Set / Get Gyro User Gain of the sensor
 */

/*!
 * \ingroup bmi270_dsdApiGyroUG
 * \page bmi270_dsd_api_bmi270_dsd_update_gyro_user_gain bmi270_dsd_update_gyro_user_gain
 * \code
 * int8_t bmi270_dsd_update_gyro_user_gain(const struct bmi2_gyro_user_gain_config *user_gain, struct bmi2_dev *dev);
 * \endcode
 * @details This API updates the gyroscope user-gain.
 *
 * @param[in] user_gain      : Structure that stores user-gain configurations.
 * @param[in] dev            : Structure instance of bmi2_dev.
 *
 * @return Result of API execution status
 * @retval 0 -> Success
 * @retval < 0 -> Fail
 */
int8_t bmi270_dsd_update_gyro_user_gain(const struct bmi2_gyro_user_gain_config *user_gain, struct bmi2_dev *dev);

/*!
 * \ingroup bmi270_dsdApiGyroUG
 * \page bmi270_dsd_api_bmi270_dsd_read_gyro_user_gain bmi270_dsd_read_gyro_user_gain
 * \code
 * int8_t bmi270_dsd_read_gyro_user_gain(struct bmi2_gyro_user_gain_data *gyr_usr_gain, const struct bmi2_dev *dev);
 * \endcode
 * @details This API reads the compensated gyroscope user-gain values.
 *
 * @param[out] gyr_usr_gain   : Structure that stores gain values.
 * @param[in]  dev            : Structure instance of bmi2_dev.
 *
 * @return Result of API execution status
 * @retval 0 -> Success
 * @retval < 0 -> Fail
 */
int8_t bmi270_dsd_read_gyro_user_gain(struct bmi2_gyro_user_gain_data *gyr_usr_gain, struct bmi2_dev *dev);

/*!
 * \ingroup bmi270_dsdApiInt
 * \page bmi270_dsd_api_bmi270_dsd_map_feat_int bmi270_dsd_map_feat_int
 * \code
 * int8_t bmi270_dsd_map_feat_int(const struct bmi2_sens_int_config *sens_int, uint8_t n_sens, struct bmi2_dev *dev)
 * \endcode
 * @details This API maps/unmaps feature interrupts to that of interrupt pins.
 *
 * @param[in] sens_int     : Structure instance of bmi2_sens_int_config.
 * @param[in] n_sens       : Number of interrupts to be mapped.
 * @param[in] dev          : Structure instance of bmi2_dev.
 *
 * @return Result of API execution status
 * @retval 0 -> Success
 * @retval < 0 -> Fail
 */
int8_t bmi270_dsd_map_feat_int(const struct bmi2_sens_int_config *sens_int, uint8_t n_sens, struct bmi2_dev *dev);

/******************************************************************************/
/*! @name       C++ Guard Macros                                      */
/******************************************************************************/
#ifdef __cplusplus
}
#endif /* End of CPP guard */

#endif /* BMI270_DSD_H_ */
