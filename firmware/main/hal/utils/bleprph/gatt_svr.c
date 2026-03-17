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

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "services/bas/ble_svc_bas.h"
#include "bleprph.h"
#include "services/ans/ble_svc_ans.h"
#include "esp_heap_caps.h"

/*** Maximum number of characteristics with the notify flag ***/
#define MAX_NOTIFY 5

/* Stack-Chan Service */
static const ble_uuid128_t stackchan_svc_uuid = BLE_UUID128_INIT(STACKCHAN_SVC_UUID_BASE);

static const ble_uuid128_t stackchan_chr_motion_uuid = BLE_UUID128_INIT(STACKCHAN_CHR_MOTION_UUID);

static const ble_uuid128_t stackchan_chr_avatar_uuid = BLE_UUID128_INIT(STACKCHAN_CHR_AVATAR_UUID);

static const ble_uuid128_t stackchan_chr_config_uuid = BLE_UUID128_INIT(STACKCHAN_CHR_CONFIG_UUID);

static const ble_uuid128_t stackchan_chr_rgb_uuid = BLE_UUID128_INIT(STACKCHAN_CHR_RGB_UUID);

/* Stack-Chan characteristic data buffers */
static char *stackchan_motion_data   = NULL;
static uint16_t stackchan_motion_len = 0;
static uint16_t stackchan_motion_handle;

static char *stackchan_avatar_data   = NULL;
static uint16_t stackchan_avatar_len = 0;
static uint16_t stackchan_avatar_handle;

static char *stackchan_config_data   = NULL;
static uint16_t stackchan_config_len = 0;
static uint16_t stackchan_config_handle;

static char *stackchan_rgb_data   = NULL;
static uint16_t stackchan_rgb_len = 0;
static uint16_t stackchan_rgb_handle;

/* Battery level */
static uint8_t battery_level = 100;
static uint16_t battery_level_handle;

/* Callback storage */
static stackchan_ble_callbacks_t g_stackchan_callbacks = {0};

/* Connection handle for notifications */
static uint16_t g_conn_handle = BLE_HS_CONN_HANDLE_NONE;

static int gatt_svc_access(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg);

static int stackchan_svc_access(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt,
                                void *arg);

static int battery_svc_access(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg);

static const struct ble_gatt_svc_def gatt_svr_svcs[] = {
    {
        /*** Stack-Chan Service ***/
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &stackchan_svc_uuid.u,
        .characteristics =
            (struct ble_gatt_chr_def[]){{
                                            /* Motion Characteristic - Read/Write/Notify */
                                            .uuid      = &stackchan_chr_motion_uuid.u,
                                            .access_cb = stackchan_svc_access,
                                            .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_NOTIFY,
                                            .val_handle = &stackchan_motion_handle,
                                        },
                                        {
                                            /* Avatar Characteristic - Read/Write/Notify */
                                            .uuid      = &stackchan_chr_avatar_uuid.u,
                                            .access_cb = stackchan_svc_access,
                                            .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_NOTIFY,
                                            .val_handle = &stackchan_avatar_handle,
                                        },
                                        {
                                            /* Config Characteristic - Read/Write/Notify */
                                            .uuid      = &stackchan_chr_config_uuid.u,
                                            .access_cb = stackchan_svc_access,
                                            .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_NOTIFY,
                                            .val_handle = &stackchan_config_handle,
                                        },
                                        {
                                            /* RGB Characteristic - Read/Write/Notify */
                                            .uuid      = &stackchan_chr_rgb_uuid.u,
                                            .access_cb = stackchan_svc_access,
                                            .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_NOTIFY,
                                            .val_handle = &stackchan_rgb_handle,
                                        },
                                        {
                                            0, /* No more characteristics */
                                        }},
    },

    {
        /*** Battery Service (standard 0x180F) ***/
        .type            = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid            = BLE_UUID16_DECLARE(0x180F),
        .characteristics = (struct ble_gatt_chr_def[]){{
                                                           /* Battery Level Characteristic (standard 0x2A19) */
                                                           .uuid       = BLE_UUID16_DECLARE(0x2A19),
                                                           .access_cb  = battery_svc_access,
                                                           .flags      = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                                                           .val_handle = &battery_level_handle,
                                                       },
                                                       {
                                                           0, /* No more characteristics */
                                                       }},
    },

    {
        0, /* No more services. */
    },
};

static int gatt_svr_write(struct os_mbuf *om, uint16_t min_len, uint16_t max_len, void *dst, uint16_t *len)
{
    uint16_t om_len;
    int rc;

    om_len = OS_MBUF_PKTLEN(om);
    if (om_len < min_len || om_len > max_len) {
        MODLOG_DFLT(ERROR, "Invalid attribute value length: %d (expected %d-%d)", om_len, min_len, max_len);
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }

    rc = ble_hs_mbuf_to_flat(om, dst, max_len, len);
    if (rc != 0) {
        MODLOG_DFLT(ERROR, "Failed to flatten mbuf: %d", rc);
        return BLE_ATT_ERR_UNLIKELY;
    }

    return 0;
}

/**
 * Stack-Chan service access callback
 */
static int stackchan_svc_access(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt,
                                void *arg)
{
    int rc;

    /* Store connection handle for notifications */
    if (conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        g_conn_handle = conn_handle;
    }

    switch (ctxt->op) {
        case BLE_GATT_ACCESS_OP_READ_CHR:
            MODLOG_DFLT(INFO, "Stack-Chan characteristic read; conn_handle=%d attr_handle=%d", conn_handle,
                        attr_handle);

            if (attr_handle == stackchan_motion_handle) {
                rc = os_mbuf_append(ctxt->om, stackchan_motion_data, stackchan_motion_len);
                return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
            } else if (attr_handle == stackchan_avatar_handle) {
                rc = os_mbuf_append(ctxt->om, stackchan_avatar_data, stackchan_avatar_len);
                return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
            } else if (attr_handle == stackchan_config_handle) {
                rc = os_mbuf_append(ctxt->om, stackchan_config_data, stackchan_config_len);
                return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
            } else if (attr_handle == stackchan_rgb_handle) {
                rc = os_mbuf_append(ctxt->om, stackchan_rgb_data, stackchan_rgb_len);
                return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
            }
            break;

        case BLE_GATT_ACCESS_OP_WRITE_CHR:
            MODLOG_DFLT(INFO, "Stack-Chan characteristic write; conn_handle=%d attr_handle=%d", conn_handle,
                        attr_handle);

            if (attr_handle == stackchan_motion_handle) {
                rc = gatt_svr_write(ctxt->om, 0, STACKCHAN_MAX_JSON_LEN, stackchan_motion_data, &stackchan_motion_len);
                if (rc == 0) {
                    stackchan_motion_data[stackchan_motion_len] = '\0';
                    // MODLOG_DFLT(INFO, "Motion data received (%d bytes): %s", stackchan_motion_len,
                    //             stackchan_motion_data);

                    /* Call user callback if registered */
                    if (g_stackchan_callbacks.motion_cb) {
                        g_stackchan_callbacks.motion_cb(stackchan_motion_data, stackchan_motion_len, conn_handle);
                    }
                }
                return rc;
            } else if (attr_handle == stackchan_avatar_handle) {
                rc = gatt_svr_write(ctxt->om, 0, STACKCHAN_MAX_JSON_LEN, stackchan_avatar_data, &stackchan_avatar_len);
                if (rc == 0) {
                    stackchan_avatar_data[stackchan_avatar_len] = '\0';
                    // MODLOG_DFLT(INFO, "Avatar data received (%d bytes): %s", stackchan_avatar_len,
                    //             stackchan_avatar_data);

                    /* Call user callback if registered */
                    if (g_stackchan_callbacks.avatar_cb) {
                        g_stackchan_callbacks.avatar_cb(stackchan_avatar_data, stackchan_avatar_len, conn_handle);
                    }
                }
                return rc;
            } else if (attr_handle == stackchan_config_handle) {
                rc = gatt_svr_write(ctxt->om, 0, STACKCHAN_MAX_JSON_LEN, stackchan_config_data, &stackchan_config_len);
                if (rc == 0) {
                    stackchan_config_data[stackchan_config_len] = '\0';
                    MODLOG_DFLT(INFO, "Config data received (%d bytes): %s", stackchan_config_len,
                                stackchan_config_data);

                    /* Call user callback if registered */
                    if (g_stackchan_callbacks.config_cb) {
                        g_stackchan_callbacks.config_cb(stackchan_config_data, stackchan_config_len, conn_handle);
                    }
                }
                return rc;
            } else if (attr_handle == stackchan_rgb_handle) {
                rc = gatt_svr_write(ctxt->om, 0, STACKCHAN_MAX_JSON_LEN, stackchan_rgb_data, &stackchan_rgb_len);
                if (rc == 0) {
                    stackchan_rgb_data[stackchan_rgb_len] = '\0';
                    // MODLOG_DFLT(INFO, "RGB data received (%d bytes): %s", stackchan_rgb_len, stackchan_rgb_data);

                    /* Call user callback if registered */
                    if (g_stackchan_callbacks.rgb_cb) {
                        g_stackchan_callbacks.rgb_cb(stackchan_rgb_data, stackchan_rgb_len, conn_handle);
                    }
                }
                return rc;
            }
            break;

        default:
            break;
    }

    return BLE_ATT_ERR_UNLIKELY;
}

/**
 * Battery service access callback
 */
static int battery_svc_access(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    int rc;

    switch (ctxt->op) {
        case BLE_GATT_ACCESS_OP_READ_CHR:
            MODLOG_DFLT(INFO, "Battery level read; conn_handle=%d attr_handle=%d", conn_handle, attr_handle);

            if (attr_handle == battery_level_handle) {
                /* Call user callback to get current battery level */
                if (g_stackchan_callbacks.battery_read_cb) {
                    battery_level = g_stackchan_callbacks.battery_read_cb();
                }

                rc = os_mbuf_append(ctxt->om, &battery_level, sizeof(battery_level));
                return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
            }
            break;

        default:
            break;
    }

    return BLE_ATT_ERR_UNLIKELY;
}

/**
 * Old GATT service access callback (kept for compatibility)
 */
static int gatt_svc_access(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    /* This can be removed if not needed */
    return BLE_ATT_ERR_UNLIKELY;
}

void gatt_svr_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg)
{
    char buf[BLE_UUID_STR_LEN];

    switch (ctxt->op) {
        case BLE_GATT_REGISTER_OP_SVC:
            MODLOG_DFLT(DEBUG, "registered service %s with handle=%d", ble_uuid_to_str(ctxt->svc.svc_def->uuid, buf),
                        ctxt->svc.handle);
            break;

        case BLE_GATT_REGISTER_OP_CHR:
            MODLOG_DFLT(DEBUG,
                        "registering characteristic %s with "
                        "def_handle=%d val_handle=%d",
                        ble_uuid_to_str(ctxt->chr.chr_def->uuid, buf), ctxt->chr.def_handle, ctxt->chr.val_handle);
            break;

        case BLE_GATT_REGISTER_OP_DSC:
            MODLOG_DFLT(DEBUG, "registering descriptor %s with handle=%d",
                        ble_uuid_to_str(ctxt->dsc.dsc_def->uuid, buf), ctxt->dsc.handle);
            break;

        default:
            assert(0);
            break;
    }
}

int gatt_svr_init(void)
{
    int rc;

    /* Allocate buffers in PSRAM */
    stackchan_motion_data = (char *)heap_caps_malloc(STACKCHAN_MAX_JSON_LEN, MALLOC_CAP_SPIRAM);
    stackchan_avatar_data = (char *)heap_caps_malloc(STACKCHAN_MAX_JSON_LEN, MALLOC_CAP_SPIRAM);
    stackchan_config_data = (char *)heap_caps_malloc(STACKCHAN_MAX_JSON_LEN, MALLOC_CAP_SPIRAM);
    stackchan_rgb_data    = (char *)heap_caps_malloc(STACKCHAN_MAX_JSON_LEN, MALLOC_CAP_SPIRAM);

    if (!stackchan_motion_data || !stackchan_avatar_data || !stackchan_config_data || !stackchan_rgb_data) {
        MODLOG_DFLT(ERROR, "Failed to allocate memory for Stack-Chan characteristics\n");
        return BLE_HS_ENOMEM;
    }

    ble_svc_gap_init();
    ble_svc_gatt_init();

    rc = ble_gatts_count_cfg(gatt_svr_svcs);
    if (rc != 0) {
        return rc;
    }

    rc = ble_gatts_add_svcs(gatt_svr_svcs);
    if (rc != 0) {
        return rc;
    }

    /* Initialize Stack-Chan data with empty JSON */
    strcpy(stackchan_motion_data, "{}");
    stackchan_motion_len = 2;

    strcpy(stackchan_avatar_data, "{}");
    stackchan_avatar_len = 2;

    strcpy(stackchan_config_data, "{}");
    stackchan_config_len = 2;

    strcpy(stackchan_rgb_data, "{}");
    stackchan_rgb_len = 2;

    return 0;
}

/**
 * Public API implementations
 */

void stackchan_ble_register_callbacks(const stackchan_ble_callbacks_t *callbacks)
{
    if (callbacks) {
        g_stackchan_callbacks = *callbacks;
        MODLOG_DFLT(INFO, "Stack-Chan callbacks registered");
    }
}

int stackchan_ble_notify_motion(const char *json_data, uint16_t len)
{
    if (!json_data || len == 0 || len >= STACKCHAN_MAX_JSON_LEN) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }

    memcpy(stackchan_motion_data, json_data, len);
    stackchan_motion_len       = len;
    stackchan_motion_data[len] = '\0';

    if (g_conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        ble_gatts_chr_updated(stackchan_motion_handle);
        MODLOG_DFLT(INFO, "Motion notification sent");
    }

    return 0;
}

int stackchan_ble_notify_avatar(const char *json_data, uint16_t len)
{
    if (!json_data || len == 0 || len >= STACKCHAN_MAX_JSON_LEN) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }

    memcpy(stackchan_avatar_data, json_data, len);
    stackchan_avatar_len       = len;
    stackchan_avatar_data[len] = '\0';

    if (g_conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        ble_gatts_chr_updated(stackchan_avatar_handle);
        MODLOG_DFLT(INFO, "Avatar notification sent");
    }

    return 0;
}

int stackchan_ble_notify_config(const char *json_data, uint16_t len)
{
    if (!json_data || len == 0 || len >= STACKCHAN_MAX_JSON_LEN) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }

    memcpy(stackchan_config_data, json_data, len);
    stackchan_config_len       = len;
    stackchan_config_data[len] = '\0';

    if (g_conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        ble_gatts_chr_updated(stackchan_config_handle);
        MODLOG_DFLT(INFO, "Config notification sent");
    }

    return 0;
}

int stackchan_ble_notify_rgb(const char *json_data, uint16_t len)
{
    if (!json_data || len == 0 || len >= STACKCHAN_MAX_JSON_LEN) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }

    memcpy(stackchan_rgb_data, json_data, len);
    stackchan_rgb_len       = len;
    stackchan_rgb_data[len] = '\0';

    if (g_conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        ble_gatts_chr_updated(stackchan_rgb_handle);
        MODLOG_DFLT(INFO, "RGB notification sent");
    }

    return 0;
}

int stackchan_ble_update_battery_level(uint8_t level)
{
    if (level > 100) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }

    battery_level = level;

    if (g_conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        ble_gatts_chr_updated(battery_level_handle);
        MODLOG_DFLT(INFO, "Battery level updated to %d%%", level);
    }

    return 0;
}

void stackchan_ble_set_conn_handle(uint16_t conn_handle)
{
    g_conn_handle = conn_handle;
    MODLOG_DFLT(INFO, "Stack-Chan connection handle updated: %d", conn_handle);
}

bool stackchan_ble_is_connected(void)
{
    return (g_conn_handle != BLE_HS_CONN_HANDLE_NONE);
}
