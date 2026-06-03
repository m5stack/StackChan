#pragma once

#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_io_spi.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t stackchan_new_panel_io_spi_shared_dc(esp_lcd_spi_bus_handle_t bus,
                                               const esp_lcd_panel_io_spi_config_t* io_config,
                                               int shared_dc_gpio_num,
                                               esp_lcd_panel_io_handle_t* ret_io);

esp_err_t stackchan_panel_io_wait_idle(esp_lcd_panel_io_handle_t io);

#ifdef __cplusplus
}
#endif
