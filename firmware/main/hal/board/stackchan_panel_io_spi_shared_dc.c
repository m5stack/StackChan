/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "stackchan_panel_io_spi_shared_dc.h"

#include <stdlib.h>
#include <string.h>
#include <sys/cdefs.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_io_interface.h"
#include "esp_log.h"
#include "esp_private/gpio.h"
#include "hal/gpio_ll.h"
#include "sdkconfig.h"
#include "soc/gpio_reg.h"
#include "soc/gpio_sig_map.h"

static const char* TAG = "stackchan.panel_io";

typedef struct {
    spi_transaction_t base;
    struct {
        unsigned int dc_gpio_level : 1;
        unsigned int en_trans_done_cb : 1;
    } flags;
} lcd_spi_trans_descriptor_t;

typedef struct {
    esp_lcd_panel_io_t base;
    spi_device_handle_t spi_dev;
    size_t spi_trans_max_bytes;
    int dc_gpio_num;
    int shared_dc_gpio_num;
    int shared_dc_signal_out_idx;
    esp_lcd_panel_io_color_trans_done_cb_t on_color_trans_done;
    void* user_ctx;
    size_t queue_size;
    size_t num_trans_inflight;
    int lcd_cmd_bits;
    int lcd_param_bits;
    struct {
        unsigned int dc_cmd_level : 1;
        unsigned int dc_data_level : 1;
        unsigned int dc_param_level : 1;
        unsigned int octal_mode : 1;
        unsigned int quad_mode : 1;
        unsigned int shared_dc_enabled : 1;
    } flags;
    lcd_spi_trans_descriptor_t trans_pool[];
} stackchan_panel_io_spi_t;

static esp_err_t panel_io_spi_rx_param(esp_lcd_panel_io_t* io, int lcd_cmd, void* param, size_t param_size);
static esp_err_t panel_io_spi_tx_param(esp_lcd_panel_io_t* io, int lcd_cmd, const void* param, size_t param_size);
static esp_err_t panel_io_spi_tx_color(esp_lcd_panel_io_t* io, int lcd_cmd, const void* color, size_t color_size);
static esp_err_t panel_io_spi_del(esp_lcd_panel_io_t* io);
static void lcd_spi_pre_trans_cb(spi_transaction_t* trans);
static void lcd_spi_post_trans_color_cb(spi_transaction_t* trans);
static esp_err_t panel_io_spi_register_event_callbacks(esp_lcd_panel_io_handle_t io,
                                                       const esp_lcd_panel_io_callbacks_t* cbs, void* user_ctx);
static esp_err_t panel_io_spi_recycle_trans_result(stackchan_panel_io_spi_t* panel_io,
                                                   spi_transaction_t** out_trans);
static esp_err_t panel_io_spi_wait_idle_locked(stackchan_panel_io_spi_t* panel_io);

static inline int shared_signal_for_host(spi_host_device_t host)
{
    switch (host) {
#ifdef SPI2_HOST
    case SPI2_HOST:
        return FSPIQ_OUT_IDX;
#endif
#ifdef SPI3_HOST
    case SPI3_HOST:
        return SPI3_Q_OUT_IDX;
#endif
    default:
        return -1;
    }
}

static inline void reverse_buffer_bytes(uint8_t* buffer, int start, int end)
{
    while (start < end) {
        uint8_t tmp   = buffer[start];
        buffer[start] = buffer[end];
        buffer[end]   = tmp;
        start++;
        end--;
    }
}

static inline void shared_dc_route_to_gpio(int gpio_num)
{
    if (gpio_num == GPIO_NUM_35) {
        REG_WRITE(GPIO_FUNC35_OUT_SEL_CFG_REG, SIG_GPIO_OUT_IDX);
    }
}

static inline void shared_dc_route_to_spi(stackchan_panel_io_spi_t* panel_io)
{
    if (panel_io->shared_dc_gpio_num == GPIO_NUM_35 && panel_io->shared_dc_signal_out_idx >= 0) {
        REG_WRITE(GPIO_FUNC35_OUT_SEL_CFG_REG, panel_io->shared_dc_signal_out_idx);
    }
}

esp_err_t stackchan_new_panel_io_spi_shared_dc(esp_lcd_spi_bus_handle_t bus,
                                               const esp_lcd_panel_io_spi_config_t* io_config,
                                               int shared_dc_gpio_num,
                                               esp_lcd_panel_io_handle_t* ret_io)
{
    esp_err_t ret                        = ESP_OK;
    stackchan_panel_io_spi_t* panel_io   = NULL;

    ESP_GOTO_ON_FALSE(bus && io_config && ret_io, ESP_ERR_INVALID_ARG, err, TAG, "invalid argument");
    panel_io = calloc(1, sizeof(stackchan_panel_io_spi_t) +
                             sizeof(lcd_spi_trans_descriptor_t) * io_config->trans_queue_depth);
    ESP_GOTO_ON_FALSE(panel_io, ESP_ERR_NO_MEM, err, TAG, "no mem for spi panel io");

    spi_device_interface_config_t devcfg = {
        .flags = SPI_DEVICE_HALFDUPLEX |
                 (io_config->flags.lsb_first ? SPI_DEVICE_TXBIT_LSBFIRST : 0) |
                 (io_config->flags.sio_mode ? SPI_DEVICE_3WIRE : 0) |
                 (io_config->flags.cs_high_active ? SPI_DEVICE_POSITIVE_CS : 0),
        .clock_speed_hz = io_config->pclk_hz,
        .mode = io_config->spi_mode,
        .spics_io_num = io_config->cs_gpio_num,
        .queue_size = io_config->trans_queue_depth,
        .pre_cb = lcd_spi_pre_trans_cb,
        .post_cb = lcd_spi_post_trans_color_cb,
        .cs_ena_pretrans = io_config->cs_ena_pretrans,
        .cs_ena_posttrans = io_config->cs_ena_posttrans,
    };
    ret = spi_bus_add_device((spi_host_device_t)bus, &devcfg, &panel_io->spi_dev);
    ESP_GOTO_ON_ERROR(ret, err, TAG, "adding spi device to bus failed");

    if (io_config->dc_gpio_num >= 0) {
        gpio_set_level(io_config->dc_gpio_num, 0);
        gpio_func_sel(io_config->dc_gpio_num, PIN_FUNC_GPIO);
        gpio_output_enable(io_config->dc_gpio_num);
    }

    panel_io->flags.dc_cmd_level = io_config->flags.dc_high_on_cmd;
    panel_io->flags.dc_data_level = !io_config->flags.dc_low_on_data;
    panel_io->flags.dc_param_level = !io_config->flags.dc_low_on_param;
    panel_io->flags.octal_mode = io_config->flags.octal_mode;
    panel_io->flags.quad_mode = io_config->flags.quad_mode;
    panel_io->on_color_trans_done = io_config->on_color_trans_done;
    panel_io->user_ctx = io_config->user_ctx;
    panel_io->lcd_cmd_bits = io_config->lcd_cmd_bits;
    panel_io->lcd_param_bits = io_config->lcd_param_bits;
    panel_io->dc_gpio_num = io_config->dc_gpio_num;
    panel_io->queue_size = io_config->trans_queue_depth;
    panel_io->base.rx_param = panel_io_spi_rx_param;
    panel_io->base.tx_param = panel_io_spi_tx_param;
    panel_io->base.tx_color = panel_io_spi_tx_color;
    panel_io->base.del = panel_io_spi_del;
    panel_io->base.register_event_callbacks = panel_io_spi_register_event_callbacks;

    panel_io->shared_dc_gpio_num = shared_dc_gpio_num;
    panel_io->shared_dc_signal_out_idx = shared_signal_for_host((spi_host_device_t)bus);
    panel_io->flags.shared_dc_enabled =
        (io_config->dc_gpio_num == shared_dc_gpio_num) && (panel_io->shared_dc_signal_out_idx >= 0);

    size_t max_trans_bytes = 0;
    ESP_GOTO_ON_ERROR(spi_bus_get_max_transaction_len((spi_host_device_t)bus, &max_trans_bytes), err, TAG,
                      "get spi max transaction len failed");
    panel_io->spi_trans_max_bytes = max_trans_bytes;

    if (panel_io->flags.shared_dc_enabled) {
        gpio_output_disable(panel_io->dc_gpio_num);
        shared_dc_route_to_spi(panel_io);
    }

    *ret_io = &panel_io->base;
    return ESP_OK;

err:
    if (panel_io) {
        if (io_config && io_config->dc_gpio_num >= 0) {
            gpio_output_disable(io_config->dc_gpio_num);
        }
        free(panel_io);
    }
    return ret;
}

static esp_err_t panel_io_spi_del(esp_lcd_panel_io_t* io)
{
    esp_err_t ret                   = ESP_OK;
    spi_transaction_t* spi_trans    = NULL;
    stackchan_panel_io_spi_t* panel_io = __containerof(io, stackchan_panel_io_spi_t, base);

    size_t num_trans_inflight = panel_io->num_trans_inflight;
    for (size_t i = 0; i < num_trans_inflight; i++) {
        ret = spi_device_get_trans_result(panel_io->spi_dev, &spi_trans, portMAX_DELAY);
        ESP_GOTO_ON_ERROR(ret, err, TAG, "recycle spi transactions failed");
        panel_io->num_trans_inflight--;
    }
    spi_bus_remove_device(panel_io->spi_dev);
    if (panel_io->dc_gpio_num >= 0) {
        gpio_output_disable(panel_io->dc_gpio_num);
    }
    free(panel_io);

err:
    return ret;
}

static esp_err_t panel_io_spi_register_event_callbacks(esp_lcd_panel_io_handle_t io,
                                                       const esp_lcd_panel_io_callbacks_t* cbs, void* user_ctx)
{
    stackchan_panel_io_spi_t* panel_io = __containerof(io, stackchan_panel_io_spi_t, base);
    panel_io->on_color_trans_done = cbs->on_color_trans_done;
    panel_io->user_ctx = user_ctx;
    return ESP_OK;
}

static esp_err_t panel_io_spi_recycle_trans_result(stackchan_panel_io_spi_t* panel_io,
                                                   spi_transaction_t** out_trans)
{
    spi_transaction_t* spi_trans = NULL;
    esp_err_t ret = spi_device_get_trans_result(panel_io->spi_dev, &spi_trans, portMAX_DELAY);
    ESP_RETURN_ON_ERROR(ret, TAG, "recycle spi transaction failed");
    panel_io->num_trans_inflight--;
    if (out_trans) {
        *out_trans = spi_trans;
    }
    return ESP_OK;
}

static esp_err_t panel_io_spi_wait_idle_locked(stackchan_panel_io_spi_t* panel_io)
{
    while (panel_io->num_trans_inflight > 0) {
        ESP_RETURN_ON_ERROR(panel_io_spi_recycle_trans_result(panel_io, NULL), TAG,
                            "recycle spi transactions failed");
    }
    return ESP_OK;
}

esp_err_t stackchan_panel_io_wait_idle(esp_lcd_panel_io_handle_t io)
{
    ESP_RETURN_ON_FALSE(io, ESP_ERR_INVALID_ARG, TAG, "invalid panel io");

    stackchan_panel_io_spi_t* panel_io = __containerof(io, stackchan_panel_io_spi_t, base);
    ESP_RETURN_ON_ERROR(spi_device_acquire_bus(panel_io->spi_dev, portMAX_DELAY), TAG, "acquire spi bus failed");
    esp_err_t ret = panel_io_spi_wait_idle_locked(panel_io);
    spi_device_release_bus(panel_io->spi_dev);
    return ret;
}

static void spi_lcd_prepare_cmd_buffer(stackchan_panel_io_spi_t* panel_io, const void* cmd)
{
    uint8_t* from = (uint8_t*)cmd;
    if (panel_io->lcd_cmd_bits > 8) {
        int start = 0;
        int end = panel_io->lcd_cmd_bits / 8 - 1;
        reverse_buffer_bytes(from, start, end);
    }
}

static void spi_lcd_prepare_param_buffer(stackchan_panel_io_spi_t* panel_io, const void* param, size_t param_size)
{
    uint8_t* from = (uint8_t*)param;
    int param_width = panel_io->lcd_param_bits / 8;
    size_t param_num = param_size / param_width;
    if (panel_io->lcd_param_bits > 8) {
        for (size_t i = 0; i < param_num; i++) {
            int start = i * param_width;
            int end = start + param_width - 1;
            reverse_buffer_bytes(from, start, end);
        }
    }
}

static esp_err_t panel_io_spi_tx_param(esp_lcd_panel_io_t* io, int lcd_cmd, const void* param, size_t param_size)
{
    esp_err_t ret                         = ESP_OK;
    lcd_spi_trans_descriptor_t* lcd_trans = NULL;
    stackchan_panel_io_spi_t* panel_io    = __containerof(io, stackchan_panel_io_spi_t, base);
    bool send_cmd                         = (lcd_cmd >= 0);

    ESP_RETURN_ON_ERROR(spi_device_acquire_bus(panel_io->spi_dev, portMAX_DELAY), TAG, "acquire spi bus failed");

    ret = panel_io_spi_wait_idle_locked(panel_io);
    ESP_GOTO_ON_ERROR(ret, err, TAG, "recycle spi transactions failed");
    lcd_trans = &panel_io->trans_pool[0];
    memset(lcd_trans, 0, sizeof(lcd_spi_trans_descriptor_t));

    lcd_trans->base.user = panel_io;
    if (param && param_size) {
        lcd_trans->base.flags |= SPI_TRANS_CS_KEEP_ACTIVE;
    }
    if (panel_io->flags.octal_mode) {
        lcd_trans->base.flags |= (SPI_TRANS_MULTILINE_CMD | SPI_TRANS_MULTILINE_ADDR | SPI_TRANS_MODE_OCT);
    }

    if (send_cmd) {
        spi_lcd_prepare_cmd_buffer(panel_io, &lcd_cmd);
        lcd_trans->flags.dc_gpio_level = panel_io->flags.dc_cmd_level;
        lcd_trans->base.length = panel_io->lcd_cmd_bits;
        lcd_trans->base.tx_buffer = &lcd_cmd;
        ret = spi_device_polling_transmit(panel_io->spi_dev, &lcd_trans->base);
        ESP_GOTO_ON_ERROR(ret, err, TAG, "spi transmit (polling) command failed");
    }

    if (param && param_size) {
        spi_lcd_prepare_param_buffer(panel_io, param, param_size);
        lcd_trans->flags.dc_gpio_level = panel_io->flags.dc_param_level;
        lcd_trans->base.length = param_size * 8;
        lcd_trans->base.tx_buffer = param;
        lcd_trans->base.flags &= ~SPI_TRANS_CS_KEEP_ACTIVE;
        ret = spi_device_polling_transmit(panel_io->spi_dev, &lcd_trans->base);
        ESP_GOTO_ON_ERROR(ret, err, TAG, "spi transmit (polling) param failed");
    }

err:
    spi_device_release_bus(panel_io->spi_dev);
    return ret;
}

static esp_err_t panel_io_spi_rx_param(esp_lcd_panel_io_t* io, int lcd_cmd, void* param, size_t param_size)
{
    esp_err_t ret                         = ESP_OK;
    lcd_spi_trans_descriptor_t* lcd_trans = NULL;
    stackchan_panel_io_spi_t* panel_io    = __containerof(io, stackchan_panel_io_spi_t, base);
    bool send_cmd                         = (lcd_cmd >= 0);

    ESP_RETURN_ON_ERROR(spi_device_acquire_bus(panel_io->spi_dev, portMAX_DELAY), TAG, "acquire spi bus failed");

    ret = panel_io_spi_wait_idle_locked(panel_io);
    ESP_GOTO_ON_ERROR(ret, err, TAG, "recycle spi transactions failed");
    lcd_trans = &panel_io->trans_pool[0];
    memset(lcd_trans, 0, sizeof(lcd_spi_trans_descriptor_t));

    lcd_trans->base.user = panel_io;
    lcd_trans->base.flags |= SPI_TRANS_CS_KEEP_ACTIVE;
    if (panel_io->flags.octal_mode) {
        lcd_trans->base.flags |= (SPI_TRANS_MULTILINE_CMD | SPI_TRANS_MULTILINE_ADDR | SPI_TRANS_MODE_OCT);
    }

    if (send_cmd) {
        spi_lcd_prepare_cmd_buffer(panel_io, &lcd_cmd);
        lcd_trans->flags.dc_gpio_level = panel_io->flags.dc_cmd_level;
        lcd_trans->base.length = panel_io->lcd_cmd_bits;
        lcd_trans->base.tx_buffer = &lcd_cmd;
        ret = spi_device_polling_transmit(panel_io->spi_dev, &lcd_trans->base);
        ESP_GOTO_ON_ERROR(ret, err, TAG, "spi transmit (polling) command failed");
    }

    if (param && param_size) {
        lcd_trans->flags.dc_gpio_level = panel_io->flags.dc_param_level;
        lcd_trans->base.length = 0;
        lcd_trans->base.tx_buffer = NULL;
        lcd_trans->base.rxlength = param_size * 8;
        lcd_trans->base.rx_buffer = param;
        lcd_trans->base.flags &= ~SPI_TRANS_CS_KEEP_ACTIVE;
        ret = spi_device_polling_transmit(panel_io->spi_dev, &lcd_trans->base);
        ESP_GOTO_ON_ERROR(ret, err, TAG, "spi transmit (polling) param failed");
    }

err:
    spi_device_release_bus(panel_io->spi_dev);
    return ret;
}

static esp_err_t panel_io_spi_tx_color(esp_lcd_panel_io_t* io, int lcd_cmd, const void* color, size_t color_size)
{
    esp_err_t ret                         = ESP_OK;
    spi_transaction_t* spi_trans          = NULL;
    lcd_spi_trans_descriptor_t* lcd_trans = NULL;
    stackchan_panel_io_spi_t* panel_io    = __containerof(io, stackchan_panel_io_spi_t, base);

    ESP_RETURN_ON_ERROR(spi_device_acquire_bus(panel_io->spi_dev, portMAX_DELAY), TAG, "acquire spi bus failed");

    bool send_cmd = (lcd_cmd >= 0);
    if (send_cmd) {
        ret = panel_io_spi_wait_idle_locked(panel_io);
        ESP_GOTO_ON_ERROR(ret, err, TAG, "recycle spi transactions failed");
        lcd_trans = &panel_io->trans_pool[0];
        memset(lcd_trans, 0, sizeof(lcd_spi_trans_descriptor_t));

        spi_lcd_prepare_cmd_buffer(panel_io, &lcd_cmd);
        lcd_trans->base.user = panel_io;
        lcd_trans->flags.dc_gpio_level = panel_io->flags.dc_cmd_level;
        lcd_trans->base.length = panel_io->lcd_cmd_bits;
        lcd_trans->base.tx_buffer = &lcd_cmd;
        if (color && color_size) {
            lcd_trans->base.flags |= SPI_TRANS_CS_KEEP_ACTIVE;
        }
        if (panel_io->flags.octal_mode) {
            lcd_trans->base.flags |= (SPI_TRANS_MULTILINE_CMD | SPI_TRANS_MULTILINE_ADDR | SPI_TRANS_MODE_OCT);
        }
        ret = spi_device_polling_transmit(panel_io->spi_dev, &lcd_trans->base);
        ESP_GOTO_ON_ERROR(ret, err, TAG, "spi transmit (polling) command failed");
    }

    do {
        size_t chunk_size = color_size;

        if (panel_io->num_trans_inflight < panel_io->queue_size) {
            lcd_trans = &panel_io->trans_pool[panel_io->num_trans_inflight];
        } else {
            ret = panel_io_spi_recycle_trans_result(panel_io, &spi_trans);
            ESP_GOTO_ON_ERROR(ret, err, TAG, "recycle spi transactions failed");
            lcd_trans = __containerof(spi_trans, lcd_spi_trans_descriptor_t, base);
        }
        memset(lcd_trans, 0, sizeof(lcd_spi_trans_descriptor_t));

        if (chunk_size > panel_io->spi_trans_max_bytes) {
            chunk_size = panel_io->spi_trans_max_bytes;
            lcd_trans->base.flags |= SPI_TRANS_CS_KEEP_ACTIVE;
        } else {
            lcd_trans->flags.en_trans_done_cb = 1;
            lcd_trans->base.flags &= ~SPI_TRANS_CS_KEEP_ACTIVE;
        }

        lcd_trans->base.user = panel_io;
        lcd_trans->flags.dc_gpio_level = panel_io->flags.dc_data_level;
        lcd_trans->base.length = chunk_size * 8;
        lcd_trans->base.tx_buffer = color;
        if (panel_io->flags.octal_mode) {
            lcd_trans->base.flags |= (SPI_TRANS_MULTILINE_CMD | SPI_TRANS_MULTILINE_ADDR | SPI_TRANS_MODE_OCT);
        } else if (panel_io->flags.quad_mode) {
            lcd_trans->base.flags |= SPI_TRANS_MODE_QIO;
        }

        ret = spi_device_queue_trans(panel_io->spi_dev, &lcd_trans->base, portMAX_DELAY);
        ESP_GOTO_ON_ERROR(ret, err, TAG, "spi transmit (queue) color failed");
        panel_io->num_trans_inflight++;

        color = (const uint8_t*)color + chunk_size;
        color_size -= chunk_size;
    } while (color_size > 0);

err:
    spi_device_release_bus(panel_io->spi_dev);
    return ret;
}

IRAM_ATTR static void lcd_spi_pre_trans_cb(spi_transaction_t* trans)
{
    stackchan_panel_io_spi_t* panel_io    = trans->user;
    lcd_spi_trans_descriptor_t* lcd_trans = __containerof(trans, lcd_spi_trans_descriptor_t, base);
    if (panel_io->dc_gpio_num >= 0) {
        if (panel_io->flags.shared_dc_enabled) {
            shared_dc_route_to_gpio(panel_io->dc_gpio_num);
        }
        gpio_ll_set_level(&GPIO, panel_io->dc_gpio_num, lcd_trans->flags.dc_gpio_level);
        gpio_ll_output_enable(&GPIO, panel_io->dc_gpio_num);
    }
}

static void lcd_spi_post_trans_color_cb(spi_transaction_t* trans)
{
    stackchan_panel_io_spi_t* panel_io    = trans->user;
    lcd_spi_trans_descriptor_t* lcd_trans = __containerof(trans, lcd_spi_trans_descriptor_t, base);

    if (panel_io->dc_gpio_num >= 0) {
        gpio_ll_output_disable(&GPIO, panel_io->dc_gpio_num);
        if (panel_io->flags.shared_dc_enabled) {
            shared_dc_route_to_spi(panel_io);
        }
    }

    if (lcd_trans->flags.en_trans_done_cb && panel_io->on_color_trans_done) {
        panel_io->on_color_trans_done(&panel_io->base, NULL, panel_io->user_ctx);
    }
}
