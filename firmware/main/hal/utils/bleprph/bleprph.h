/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#ifndef H_BLEPRPH_
#define H_BLEPRPH_

#include <stdbool.h>
#include "nimble/ble.h"
#include "modlog/modlog.h"
#include "nimble_peripheral_utils/esp_peripheral.h"
#ifdef __cplusplus
extern "C" {
#endif

struct ble_hs_cfg;
struct ble_gatt_register_ctxt;

/** GATT server. */
#define GATT_SVR_SVC_ALERT_UUID             0x1811
#define GATT_SVR_CHR_SUP_NEW_ALERT_CAT_UUID 0x2A47
#define GATT_SVR_CHR_NEW_ALERT              0x2A46
#define GATT_SVR_CHR_SUP_UNR_ALERT_CAT_UUID 0x2A48
#define GATT_SVR_CHR_UNR_ALERT_STAT_UUID    0x2A45
#define GATT_SVR_CHR_ALERT_NOT_CTRL_PT      0x2A44

/** Stack-Chan Service UUIDs */
// Service UUID: e2e5e5e0-1234-5678-1234-56789abcdef0
#define STACKCHAN_SVC_UUID_BASE \
    0xf0, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12, 0xe0, 0xe5, 0xe5, 0xe2

// Motion Characteristic UUID: e2e5e5e1-1234-5678-1234-56789abcdef0
#define STACKCHAN_CHR_MOTION_UUID \
    0xf0, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12, 0xe1, 0xe5, 0xe5, 0xe2

// Avatar Characteristic UUID: e2e5e5e2-1234-5678-1234-56789abcdef0
#define STACKCHAN_CHR_AVATAR_UUID \
    0xf0, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12, 0xe2, 0xe5, 0xe5, 0xe2

// Config Characteristic UUID: e2e5e5e3-1234-5678-1234-56789abcdef0
#define STACKCHAN_CHR_CONFIG_UUID \
    0xf0, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12, 0xe3, 0xe5, 0xe5, 0xe2

// Animation Characteristic UUID: e2e5e5e4-1234-5678-1234-56789abcdef0
#define STACKCHAN_CHR_ANIMATION_UUID \
    0xf0, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12, 0xe4, 0xe5, 0xe5, 0xe2

/** Maximum JSON payload size for Stack-Chan characteristics */
#define STACKCHAN_MAX_JSON_LEN 2048

/**
 * Stack-Chan callback function types
 *
 * @param json_data  Pointer to JSON string received
 * @param len        Length of JSON string
 * @param conn_handle BLE connection handle
 * @return           0 on success, error code otherwise
 */
typedef int (*stackchan_ble_motion_callback_t)(const char *json_data, uint16_t len, uint16_t conn_handle);
typedef int (*stackchan_ble_avatar_callback_t)(const char *json_data, uint16_t len, uint16_t conn_handle);
typedef int (*stackchan_ble_config_callback_t)(const char *json_data, uint16_t len, uint16_t conn_handle);
typedef int (*stackchan_ble_animation_callback_t)(const char *json_data, uint16_t len, uint16_t conn_handle);

/**
 * Battery level callback function type
 *
 * @return Battery level (0-100)
 */
typedef uint8_t (*stackchan_ble_battery_read_callback_t)(void);

/**
 * Stack-Chan callback configuration structure
 */
typedef struct {
    stackchan_ble_motion_callback_t motion_cb;
    stackchan_ble_avatar_callback_t avatar_cb;
    stackchan_ble_config_callback_t config_cb;
    stackchan_ble_animation_callback_t animation_cb;
    stackchan_ble_battery_read_callback_t battery_read_cb;
} stackchan_ble_callbacks_t;

/**
 * Register Stack-Chan service callbacks
 *
 * @param callbacks  Pointer to callbacks structure
 */
void stackchan_ble_register_callbacks(const stackchan_ble_callbacks_t *callbacks);

/**
 * Send motion data notification to connected client
 *
 * @param json_data  JSON string to send
 * @param len        Length of JSON string
 * @return           0 on success, error code otherwise
 */
int stackchan_ble_notify_motion(const char *json_data, uint16_t len);

/**
 * Send avatar data notification to connected client
 *
 * @param json_data  JSON string to send
 * @param len        Length of JSON string
 * @return           0 on success, error code otherwise
 */
int stackchan_ble_notify_avatar(const char *json_data, uint16_t len);

/**
 * Send config data notification to connected client
 *
 * @param json_data  JSON string to send
 * @param len        Length of JSON string
 * @return           0 on success, error code otherwise
 */
int stackchan_ble_notify_config(const char *json_data, uint16_t len);

/**
 * Send animation data notification to connected client
 *
 * @param json_data  JSON string to send
 * @param len        Length of JSON string
 * @return           0 on success, error code otherwise
 */
int stackchan_ble_notify_animation(const char *json_data, uint16_t len);

/**
 * Update battery level and notify if subscribed
 *
 * @param level  Battery level (0-100)
 * @return       0 on success, error code otherwise
 */
int stackchan_ble_update_battery_level(uint8_t level);

/**
 * Set BLE connection handle (called internally by GAP event handler)
 *
 * @param conn_handle  BLE connection handle
 */
void stackchan_ble_set_conn_handle(uint16_t conn_handle);

/**
 * Get current BLE connection status
 *
 * @return  true if connected, false otherwise
 */
bool stackchan_ble_is_connected(void);

void gatt_svr_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg);
int gatt_svr_init(void);

void ble_prph_init(void);

#ifdef __cplusplus
}
#endif

#endif
