#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <algorithm>
#include <list>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/spi_master.h"
#include "driver/uart.h"
#include "esp_err.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_ili9341.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_video_device.h"
#include "esp_video_init.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "human_face_detect.hpp"
#include "linux/videodev2.h"

namespace {

constexpr char TAG[] = "vision_demo";

constexpr gpio_num_t AUDIO_CODEC_I2C_SDA_PIN = GPIO_NUM_12;
constexpr gpio_num_t AUDIO_CODEC_I2C_SCL_PIN = GPIO_NUM_11;

constexpr gpio_num_t CAMERA_PIN_PWDN  = GPIO_NUM_NC;
constexpr gpio_num_t CAMERA_PIN_RESET = GPIO_NUM_NC;
constexpr gpio_num_t CAMERA_PIN_XCLK  = GPIO_NUM_NC;
constexpr gpio_num_t CAMERA_PIN_D0    = GPIO_NUM_39;
constexpr gpio_num_t CAMERA_PIN_D1    = GPIO_NUM_40;
constexpr gpio_num_t CAMERA_PIN_D2    = GPIO_NUM_41;
constexpr gpio_num_t CAMERA_PIN_D3    = GPIO_NUM_42;
constexpr gpio_num_t CAMERA_PIN_D4    = GPIO_NUM_15;
constexpr gpio_num_t CAMERA_PIN_D5    = GPIO_NUM_16;
constexpr gpio_num_t CAMERA_PIN_D6    = GPIO_NUM_48;
constexpr gpio_num_t CAMERA_PIN_D7    = GPIO_NUM_47;
constexpr gpio_num_t CAMERA_PIN_VSYNC = GPIO_NUM_46;
constexpr gpio_num_t CAMERA_PIN_HREF  = GPIO_NUM_38;
constexpr gpio_num_t CAMERA_PIN_PCLK  = GPIO_NUM_45;

constexpr uint32_t XCLK_FREQ_HZ = 20000000;

constexpr int DISPLAY_WIDTH = 320;
constexpr int DISPLAY_HEIGHT = 240;
constexpr spi_host_device_t DISPLAY_SPI_HOST = SPI3_HOST;
constexpr gpio_num_t DISPLAY_SPI_MOSI_PIN = GPIO_NUM_37;
constexpr gpio_num_t DISPLAY_SPI_SCLK_PIN = GPIO_NUM_36;
constexpr gpio_num_t DISPLAY_SPI_CS_PIN = GPIO_NUM_3;
constexpr gpio_num_t DISPLAY_SPI_DC_PIN = GPIO_NUM_35;
constexpr int DISPLAY_SPI_PCLK_HZ = 40 * 1000 * 1000;
constexpr int DISPLAY_UPDATE_INTERVAL_MS = 66;
constexpr int DISPLAY_DOT_RADIUS = 6;
constexpr int DISPLAY_DOT_PATCH_RADIUS = 10;
constexpr uint16_t DISPLAY_COLOR_BG = 0x0000;
constexpr uint16_t DISPLAY_COLOR_GRID = 0x39e7;
constexpr uint16_t DISPLAY_COLOR_TRACKING = 0x07e0;
constexpr uint16_t DISPLAY_COLOR_HELD = 0xffe0;
constexpr uint16_t DISPLAY_COLOR_DOT_RING = 0xffff;

constexpr int ACQUIRE_DETECT_INTERVAL_MS = 350;
constexpr int DETECT_INTERVAL_MS = 650;
constexpr float DETECT_SCORE_THR = 0.22f;
constexpr float TRACK_SCORE_THR  = 0.22f;
constexpr float SMOOTH_ALPHA     = 0.38f;
constexpr float SMOOTH_ALPHA_STILL = 0.18f;
constexpr float SMOOTH_ALPHA_LOW_SCORE = 0.22f;
constexpr float SMOOTH_ALPHA_BIG_JUMP = 0.26f;
constexpr float TRACK_MEASUREMENT_DEADBAND = 0.012f;
constexpr float TRACK_STILL_DISTANCE = 0.055f;
constexpr float FAST_TRACK_ALPHA = 0.28f;
constexpr float FAST_TRACK_X_DEADBAND = 0.020f;
constexpr float FAST_TRACK_ANCHOR_DELTA = 0.200f;
constexpr float FAST_TRACK_MAX_X_DELTA = 0.35f;
constexpr float LOST_DECAY       = 0.98f;
constexpr int LOST_HOLD_DETECTIONS = 1;
constexpr float TRACK_OUTLIER_DISTANCE = 0.70f;
constexpr float TRACK_OUTLIER_ACCEPT_SCORE = 0.55f;
constexpr int TRACK_OUTLIER_HOLD_DETECTIONS = 1;
constexpr float FACE_INIT_SCORE_THR = 0.26f;
constexpr float FACE_EDGE_ACCEPT_SCORE = 0.42f;
constexpr float FACE_CONTINUITY_PENALTY = 0.30f;
constexpr float FACE_MIN_AREA_RATIO = 0.015f;
constexpr float FACE_MIN_ASPECT = 0.38f;
constexpr float FACE_MAX_ASPECT = 1.45f;
constexpr int FACE_EDGE_MARGIN_PX = 5;
constexpr int FAST_TRACK_MAX_AGE_MS = 1200;
constexpr float FAST_TRACK_ROI_SCALE = 1.25f;
constexpr int FAST_TRACK_SAMPLE_STEP = 4;
constexpr int FAST_TRACK_MIN_SKIN_PIXELS = 6;
constexpr int FAST_TRACK_LOG_INTERVAL_MS = 300;

constexpr uart_port_t SERVO_UART = UART_NUM_1;
constexpr int SERVO_UART_BAUD    = 1000000;
constexpr int SERVO_UART_TX_PIN  = 6;
constexpr int SERVO_UART_RX_PIN  = 7;

constexpr uint8_t SCS_INST_WRITE       = 0x03;
constexpr uint8_t SCS_INST_READ        = 0x02;
constexpr uint8_t SCS_TORQUE_ENABLE    = 40;
constexpr uint8_t SCS_GOAL_POSITION_L  = 42;
constexpr uint8_t SCS_PRESENT_POSITION_L = 56;
constexpr uint8_t SERVO_YAW_ID         = 1;
constexpr uint8_t SERVO_PITCH_ID       = 2;
constexpr int SERVO_YAW_ZERO_POS       = 460;
constexpr int SERVO_PITCH_ZERO_POS     = 620;
constexpr int SERVO_RAW_MIN            = 0;
constexpr int SERVO_RAW_MAX            = 1000;
constexpr int SERVO_YAW_LIMIT          = 520;
constexpr int SERVO_PITCH_CENTER       = 450;
constexpr int SERVO_PITCH_MIN          = 120;
constexpr int SERVO_PITCH_MAX          = 760;
constexpr int SERVO_COMMAND_TIME       = 24;
constexpr int SERVO_UPDATE_INTERVAL_MS = 28;
constexpr int SERVO_MIN_DELTA          = 2;
constexpr int SERVO_MAX_YAW_STEP       = 78;
constexpr int SERVO_MAX_PITCH_STEP     = 42;
constexpr float SERVO_CENTER_DEADBAND_X = 0.045f;
constexpr float SERVO_CENTER_DEADBAND_Y = 0.080f;
constexpr float SERVO_YAW_P_GAIN        = 135.0f;
constexpr float SERVO_PITCH_P_GAIN      = 58.0f;
constexpr float SERVO_FACE_WIDTH_REF_PX = 70.0f;
constexpr float SERVO_FACE_SCALE_MIN    = 0.55f;
constexpr float SERVO_FACE_SCALE_MAX    = 1.25f;
constexpr bool ENABLE_FRAME_DIAGNOSTICS = false;
constexpr bool ENABLE_LIVE_FORMAT_PROBE = false;
constexpr bool ENABLE_SERVO_SELF_TEST   = false;
constexpr bool ENABLE_SERVO_TRACK_READBACK = false;

constexpr uint8_t PY32_ADDR       = 0x6f;
constexpr uint8_t PY32_REG_VERSION = 0x02;
constexpr uint8_t PY32_REG_GPIO_M_L = 0x03;
constexpr uint8_t PY32_REG_GPIO_O_L = 0x05;
constexpr uint8_t PY32_REG_GPIO_PU_L = 0x09;
constexpr uint8_t PY32_REG_GPIO_PD_L = 0x0b;
constexpr uint8_t AW9523_ADDR       = 0x58;

extern const uint8_t test_face_320x240_rgb565_start[] asm("_binary_test_face_320x240_rgb565_start");
extern const uint8_t test_face_320x240_rgb565_end[] asm("_binary_test_face_320x240_rgb565_end");

struct MmapBuffer {
    void* start  = nullptr;
    size_t length = 0;
};

struct TrackingState {
    float x          = 0.0f;
    float y          = 0.0f;
    bool initialized = false;
    int lost_count   = 0;
    bool box_valid   = false;
    int64_t last_face_us = 0;
    float last_face_x = 0.0f;
    float last_face_y = 0.0f;
    int box[4]       = {0, 0, 0, 0};
};

struct ServoState {
    int yaw_angle       = 0;
    int pitch_angle     = SERVO_PITCH_CENTER;
    int64_t last_cmd_us = 0;
    bool initialized    = false;
};

struct DebugDisplay {
    esp_lcd_panel_io_handle_t panel_io = nullptr;
    esp_lcd_panel_handle_t panel       = nullptr;
    uint16_t* line_buffer              = nullptr;
    uint16_t* patch_buffer             = nullptr;
    int last_dot_x                     = -1;
    int last_dot_y                     = -1;
    bool initialized                   = false;
};

struct CameraState {
    int fd              = -1;
    uint32_t pixel_fmt  = 0;
    uint32_t width      = 0;
    uint32_t height     = 0;
    MmapBuffer buffers[2];
    uint32_t buffer_cnt = 0;
};

const char* fourcc_to_str(uint32_t pixelformat, char out[5]);
bool has_recent_face_box(const TrackingState& state, int64_t now_us);
void update_servo_tracking(ServoState& servo, const TrackingState& tracking, int64_t now_us);
void servo_control_task(void* arg);
void debug_display_task(void* arg);

SemaphoreHandle_t g_tracking_mutex = nullptr;
TrackingState g_shared_tracking;
int64_t g_shared_tracking_us = 0;
DebugDisplay g_debug_display;

uint8_t sample_luma(const CameraState& camera, const uint8_t* data, uint32_t x, uint32_t y)
{
    if (x >= camera.width) {
        x = camera.width - 1;
    }
    if (y >= camera.height) {
        y = camera.height - 1;
    }

    switch (camera.pixel_fmt) {
        case V4L2_PIX_FMT_RGB565:
        case V4L2_PIX_FMT_RGB565X: {
            const size_t offset = (static_cast<size_t>(y) * camera.width + x) * 2;
            uint16_t pixel;
            if (camera.pixel_fmt == V4L2_PIX_FMT_RGB565) {
                pixel = static_cast<uint16_t>(data[offset]) | (static_cast<uint16_t>(data[offset + 1]) << 8);
            } else {
                pixel = (static_cast<uint16_t>(data[offset]) << 8) | static_cast<uint16_t>(data[offset + 1]);
            }
            const uint8_t r = static_cast<uint8_t>(((pixel >> 11) & 0x1f) * 255 / 31);
            const uint8_t g = static_cast<uint8_t>(((pixel >> 5) & 0x3f) * 255 / 63);
            const uint8_t b = static_cast<uint8_t>((pixel & 0x1f) * 255 / 31);
            return static_cast<uint8_t>((77 * r + 150 * g + 29 * b) >> 8);
        }
        case V4L2_PIX_FMT_YUYV: {
            const size_t offset = (static_cast<size_t>(y) * camera.width + x) * 2;
            return data[offset];
        }
        case V4L2_PIX_FMT_GREY:
            return data[static_cast<size_t>(y) * camera.width + x];
        default:
            return 0;
    }
}

void log_frame_probe(const CameraState& camera, const uint8_t* data)
{
    constexpr uint32_t PROBE_W = 32;
    constexpr uint32_t PROBE_H = 16;
    constexpr char RAMP[]      = " .:-=+*#%@";

    uint32_t sum = 0;
    uint8_t min_luma = 255;
    uint8_t max_luma = 0;
    char line[PROBE_W + 1];
    line[PROBE_W] = '\0';

    ESP_LOGI(TAG, "frame probe thumbnail follows");
    for (uint32_t row = 0; row < PROBE_H; ++row) {
        for (uint32_t col = 0; col < PROBE_W; ++col) {
            const uint32_t x = col * camera.width / PROBE_W;
            const uint32_t y = row * camera.height / PROBE_H;
            const uint8_t y8 = sample_luma(camera, data, x, y);
            sum += y8;
            min_luma = std::min(min_luma, y8);
            max_luma = std::max(max_luma, y8);
            line[col] = RAMP[y8 * (sizeof(RAMP) - 2) / 255];
        }
        ESP_LOGI(TAG, "probe |%s|", line);
    }
    ESP_LOGI(TAG, "frame probe luma avg=%lu min=%u max=%u span=%u", sum / (PROBE_W * PROBE_H), min_luma, max_luma,
             static_cast<unsigned>(max_luma - min_luma));
}

void dump_frame_pgm_hex(const CameraState& camera, const uint8_t* data)
{
    constexpr uint32_t DUMP_W = 160;
    constexpr uint32_t DUMP_H = 120;
    char line[DUMP_W * 2 + 1];
    line[DUMP_W * 2] = '\0';

    printf("FRAME_PGM_BEGIN %lu %lu\n", DUMP_W, DUMP_H);
    for (uint32_t row = 0; row < DUMP_H; ++row) {
        for (uint32_t col = 0; col < DUMP_W; ++col) {
            const uint32_t x = col * camera.width / DUMP_W;
            const uint32_t y = row * camera.height / DUMP_H;
            const uint8_t y8 = sample_luma(camera, data, x, y);
            static constexpr char HEX[] = "0123456789abcdef";
            line[col * 2] = HEX[y8 >> 4];
            line[col * 2 + 1] = HEX[y8 & 0x0f];
        }
        printf("FRAME_PGM_ROW %03lu %s\n", row, line);
    }
    printf("FRAME_PGM_END\n");
    fflush(stdout);
}

void dump_frame_rgb565_hex(const CameraState& camera, const uint8_t* data)
{
    if (camera.pixel_fmt != V4L2_PIX_FMT_RGB565 && camera.pixel_fmt != V4L2_PIX_FMT_RGB565X) {
        char fourcc[5];
        ESP_LOGW(TAG, "FRAME_RGB565 skipped for pixel format %s", fourcc_to_str(camera.pixel_fmt, fourcc));
        return;
    }

    constexpr uint32_t DUMP_W = 160;
    constexpr uint32_t DUMP_H = 120;
    char line[DUMP_W * 4 + 1];
    line[DUMP_W * 4] = '\0';

    printf("FRAME_RGB565_BEGIN %lu %lu\n", DUMP_W, DUMP_H);
    for (uint32_t row = 0; row < DUMP_H; ++row) {
        for (uint32_t col = 0; col < DUMP_W; ++col) {
            const uint32_t x = col * camera.width / DUMP_W;
            const uint32_t y = row * camera.height / DUMP_H;
            const size_t offset = (static_cast<size_t>(y) * camera.width + x) * 2;
            const uint8_t lo = data[offset];
            const uint8_t hi = data[offset + 1];
            static constexpr char HEX[] = "0123456789abcdef";
            line[col * 4] = HEX[lo >> 4];
            line[col * 4 + 1] = HEX[lo & 0x0f];
            line[col * 4 + 2] = HEX[hi >> 4];
            line[col * 4 + 3] = HEX[hi & 0x0f];
        }
        printf("FRAME_RGB565_ROW %03lu %s\n", row, line);
    }
    printf("FRAME_RGB565_END\n");
    fflush(stdout);
}

float clamp_unit(float value)
{
    if (value < -1.0f) {
        return -1.0f;
    }
    if (value > 1.0f) {
        return 1.0f;
    }
    return value;
}

int normalized_to_screen_x(float value)
{
    return std::clamp(static_cast<int>(lroundf((clamp_unit(value) + 1.0f) * 0.5f * (DISPLAY_WIDTH - 1))),
                      0,
                      DISPLAY_WIDTH - 1);
}

int normalized_to_screen_y(float value)
{
    return std::clamp(static_cast<int>(lroundf((clamp_unit(value) + 1.0f) * 0.5f * (DISPLAY_HEIGHT - 1))),
                      0,
                      DISPLAY_HEIGHT - 1);
}

void publish_tracking_state(const TrackingState& tracking, int64_t now_us)
{
    if (g_tracking_mutex == nullptr) {
        return;
    }
    if (xSemaphoreTake(g_tracking_mutex, pdMS_TO_TICKS(2)) != pdTRUE) {
        return;
    }
    g_shared_tracking = tracking;
    g_shared_tracking_us = now_us;
    xSemaphoreGive(g_tracking_mutex);
}

bool read_tracking_state(TrackingState& tracking, int64_t& update_us)
{
    if (g_tracking_mutex == nullptr) {
        return false;
    }
    if (xSemaphoreTake(g_tracking_mutex, pdMS_TO_TICKS(2)) != pdTRUE) {
        return false;
    }
    tracking = g_shared_tracking;
    update_us = g_shared_tracking_us;
    xSemaphoreGive(g_tracking_mutex);
    return true;
}

const char* fourcc_to_str(uint32_t pixelformat, char out[5])
{
    out[0] = static_cast<char>(pixelformat & 0xff);
    out[1] = static_cast<char>((pixelformat >> 8) & 0xff);
    out[2] = static_cast<char>((pixelformat >> 16) & 0xff);
    out[3] = static_cast<char>((pixelformat >> 24) & 0xff);
    out[4] = '\0';
    return out;
}

esp_err_t i2c_write_reg(i2c_master_bus_handle_t bus, uint8_t addr, uint8_t reg, uint8_t value)
{
    i2c_master_dev_handle_t dev = nullptr;
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = addr,
        .scl_speed_hz    = 100000,
        .scl_wait_us     = 0,
        .flags           = {},
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &dev_cfg, &dev), TAG, "add i2c device 0x%02x failed", addr);
    uint8_t data[2] = {reg, value};
    esp_err_t ret   = i2c_master_transmit(dev, data, sizeof(data), pdMS_TO_TICKS(100));
    i2c_master_bus_rm_device(dev);
    return ret;
}

esp_err_t i2c_read_reg(i2c_master_bus_handle_t bus, uint8_t addr, uint8_t reg, uint8_t* value)
{
    i2c_master_dev_handle_t dev = nullptr;
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = addr,
        .scl_speed_hz    = 100000,
        .scl_wait_us     = 0,
        .flags           = {},
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &dev_cfg, &dev), TAG, "add i2c device 0x%02x failed", addr);
    esp_err_t ret = i2c_master_transmit_receive(dev, &reg, 1, value, 1, pdMS_TO_TICKS(100));
    i2c_master_bus_rm_device(dev);
    return ret;
}

esp_err_t init_i2c(i2c_master_bus_handle_t* bus)
{
    i2c_master_bus_config_t i2c_bus_cfg = {
        .i2c_port          = I2C_NUM_1,
        .sda_io_num        = AUDIO_CODEC_I2C_SDA_PIN,
        .scl_io_num        = AUDIO_CODEC_I2C_SCL_PIN,
        .clk_source        = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .intr_priority     = 0,
        .trans_queue_depth = 0,
        .flags =
            {
                .enable_internal_pullup = 1,
                .allow_pd                = 0,
            },
    };
    return i2c_new_master_bus(&i2c_bus_cfg, bus);
}

void init_axp2101_minimal(i2c_master_bus_handle_t bus)
{
    constexpr uint8_t AXP2101_ADDR = 0x34;
    uint8_t reg90                  = 0;
    if (i2c_read_reg(bus, AXP2101_ADDR, 0x90, &reg90) != ESP_OK) {
        ESP_LOGW(TAG, "AXP2101 not found; continuing because camera may already be powered");
        return;
    }

    i2c_write_reg(bus, AXP2101_ADDR, 0x90, reg90 | 0b10110100);
    i2c_write_reg(bus, AXP2101_ADDR, 0x97, 0b11110 - 2);
    i2c_write_reg(bus, AXP2101_ADDR, 0x69, 0b00110101);
    i2c_write_reg(bus, AXP2101_ADDR, 0x30, 0b111111);
    i2c_write_reg(bus, AXP2101_ADDR, 0x90, 0xBF);
    i2c_write_reg(bus, AXP2101_ADDR, 0x94, 33 - 5);
    i2c_write_reg(bus, AXP2101_ADDR, 0x95, 33 - 5);
    i2c_write_reg(bus, AXP2101_ADDR, 0x99, 28);
    i2c_write_reg(bus, AXP2101_ADDR, 0x27, 0x00);
    ESP_LOGI(TAG, "AXP2101 minimal rail setup done");
}

bool init_aw9523_minimal(i2c_master_bus_handle_t bus)
{
    // Mirrors the board init sequence. These expander outputs keep several board
    // peripherals in the expected reset/power state before camera probing.
    for (int attempt = 1; attempt <= 8; ++attempt) {
        if (i2c_write_reg(bus, AW9523_ADDR, 0x02, 0b00000111) == ESP_OK) {
            i2c_write_reg(bus, AW9523_ADDR, 0x03, 0b10001111);
            i2c_write_reg(bus, AW9523_ADDR, 0x04, 0b00011000);
            i2c_write_reg(bus, AW9523_ADDR, 0x05, 0b00001100);
            i2c_write_reg(bus, AW9523_ADDR, 0x11, 0b00010000);
            i2c_write_reg(bus, AW9523_ADDR, 0x12, 0xff);
            i2c_write_reg(bus, AW9523_ADDR, 0x13, 0xff);
            ESP_LOGI(TAG, "AW9523 minimal board GPIO setup done on attempt %d", attempt);
            vTaskDelay(pdMS_TO_TICKS(80));
            return true;
        }
        ESP_LOGW(TAG, "AW9523 setup attempt %d failed", attempt);
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    ESP_LOGW(TAG, "AW9523 not found; camera power/reset GPIOs are not ready");
    return false;
}

void reset_lcd_with_aw9523(i2c_master_bus_handle_t bus)
{
    ESP_LOGI(TAG, "reset LCD via AW9523");
    i2c_write_reg(bus, AW9523_ADDR, 0x03, 0b10000001);
    vTaskDelay(pdMS_TO_TICKS(20));
    i2c_write_reg(bus, AW9523_ADDR, 0x03, 0b10000011);
    vTaskDelay(pdMS_TO_TICKS(10));
}

esp_err_t init_debug_display(i2c_master_bus_handle_t i2c_bus, DebugDisplay& display)
{
    spi_bus_config_t buscfg = {};
    buscfg.mosi_io_num = DISPLAY_SPI_MOSI_PIN;
    buscfg.miso_io_num = GPIO_NUM_NC;
    buscfg.sclk_io_num = DISPLAY_SPI_SCLK_PIN;
    buscfg.quadwp_io_num = GPIO_NUM_NC;
    buscfg.quadhd_io_num = GPIO_NUM_NC;
    buscfg.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
    esp_err_t ret = spi_bus_initialize(DISPLAY_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "debug display SPI bus init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    esp_lcd_panel_io_spi_config_t io_config = {};
    io_config.cs_gpio_num = DISPLAY_SPI_CS_PIN;
    io_config.dc_gpio_num = DISPLAY_SPI_DC_PIN;
    io_config.spi_mode = 2;
    io_config.pclk_hz = DISPLAY_SPI_PCLK_HZ;
    io_config.trans_queue_depth = 1;
    io_config.lcd_cmd_bits = 8;
    io_config.lcd_param_bits = 8;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi(DISPLAY_SPI_HOST, &io_config, &display.panel_io),
                        TAG,
                        "create debug display IO failed");

    esp_lcd_panel_dev_config_t panel_config = {};
    panel_config.reset_gpio_num = GPIO_NUM_NC;
    panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR;
    panel_config.bits_per_pixel = 16;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_ili9341(display.panel_io, &panel_config, &display.panel),
                        TAG,
                        "create ILI9341 panel failed");

    esp_lcd_panel_reset(display.panel);
    reset_lcd_with_aw9523(i2c_bus);
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(display.panel), TAG, "debug display panel init failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(display.panel, true), TAG, "debug display invert failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_swap_xy(display.panel, false), TAG, "debug display swap_xy failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(display.panel, false, false), TAG, "debug display mirror failed");

    display.line_buffer = static_cast<uint16_t*>(
        heap_caps_malloc(DISPLAY_WIDTH * sizeof(uint16_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
    constexpr int PATCH_SIDE = DISPLAY_DOT_PATCH_RADIUS * 2 + 1;
    display.patch_buffer = static_cast<uint16_t*>(
        heap_caps_malloc(PATCH_SIDE * PATCH_SIDE * sizeof(uint16_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
    if (display.line_buffer == nullptr || display.patch_buffer == nullptr) {
        ESP_LOGW(TAG, "debug display buffer allocation failed");
        return ESP_ERR_NO_MEM;
    }

    esp_err_t on_ret = esp_lcd_panel_disp_on_off(display.panel, true);
    if (on_ret != ESP_ERR_NOT_SUPPORTED) {
        ESP_RETURN_ON_ERROR(on_ret, TAG, "debug display on failed");
    }
    display.initialized = true;
    ESP_LOGI(TAG, "debug display ready");
    return ESP_OK;
}

void draw_debug_background(DebugDisplay& display)
{
    if (!display.initialized) {
        return;
    }
    const int center_x = DISPLAY_WIDTH / 2;
    const int center_y = DISPLAY_HEIGHT / 2;
    for (int y = 0; y < DISPLAY_HEIGHT; ++y) {
        for (int x = 0; x < DISPLAY_WIDTH; ++x) {
            display.line_buffer[x] = (x == center_x || y == center_y) ? DISPLAY_COLOR_GRID : DISPLAY_COLOR_BG;
        }
        esp_lcd_panel_draw_bitmap(display.panel, 0, y, DISPLAY_WIDTH, y + 1, display.line_buffer);
    }
}

void draw_dot_patch(DebugDisplay& display, int cx, int cy, bool draw_dot, uint16_t dot_color)
{
    if (!display.initialized) {
        return;
    }

    const int x0 = std::clamp(cx - DISPLAY_DOT_PATCH_RADIUS, 0, DISPLAY_WIDTH - 1);
    const int x1 = std::clamp(cx + DISPLAY_DOT_PATCH_RADIUS, 0, DISPLAY_WIDTH - 1);
    const int y0 = std::clamp(cy - DISPLAY_DOT_PATCH_RADIUS, 0, DISPLAY_HEIGHT - 1);
    const int y1 = std::clamp(cy + DISPLAY_DOT_PATCH_RADIUS, 0, DISPLAY_HEIGHT - 1);
    const int w = x1 - x0 + 1;
    const int h = y1 - y0 + 1;
    const int center_x = DISPLAY_WIDTH / 2;
    const int center_y = DISPLAY_HEIGHT / 2;

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const int sx = x0 + x;
            const int sy = y0 + y;
            uint16_t color = (sx == center_x || sy == center_y) ? DISPLAY_COLOR_GRID : DISPLAY_COLOR_BG;
            if (draw_dot) {
                const int dx = sx - cx;
                const int dy = sy - cy;
                const int d2 = dx * dx + dy * dy;
                if (d2 <= DISPLAY_DOT_RADIUS * DISPLAY_DOT_RADIUS) {
                    color = dot_color;
                } else if (d2 <= (DISPLAY_DOT_RADIUS + 2) * (DISPLAY_DOT_RADIUS + 2)) {
                    color = DISPLAY_COLOR_DOT_RING;
                }
            }
            display.patch_buffer[y * w + x] = color;
        }
    }

    esp_lcd_panel_draw_bitmap(display.panel, x0, y0, x1 + 1, y1 + 1, display.patch_buffer);
}

void update_debug_display_dot(DebugDisplay& display, const TrackingState& tracking, int64_t now_us)
{
    if (!display.initialized) {
        return;
    }

    const bool recent_face = tracking.initialized && has_recent_face_box(tracking, now_us);
    const bool held_face = tracking.initialized && tracking.lost_count <= 8;
    if (!recent_face && !held_face) {
        if (display.last_dot_x >= 0 && display.last_dot_y >= 0) {
            draw_dot_patch(display, display.last_dot_x, display.last_dot_y, false, DISPLAY_COLOR_BG);
            display.last_dot_x = -1;
            display.last_dot_y = -1;
        }
        return;
    }

    const int dot_x = normalized_to_screen_x(tracking.x);
    const int dot_y = normalized_to_screen_y(tracking.y);
    if (display.last_dot_x >= 0 && display.last_dot_y >= 0 &&
        (dot_x != display.last_dot_x || dot_y != display.last_dot_y)) {
        draw_dot_patch(display, display.last_dot_x, display.last_dot_y, false, DISPLAY_COLOR_BG);
    }

    const uint16_t dot_color = recent_face ? DISPLAY_COLOR_TRACKING : DISPLAY_COLOR_HELD;
    draw_dot_patch(display, dot_x, dot_y, true, dot_color);
    display.last_dot_x = dot_x;
    display.last_dot_y = dot_y;
}

void init_servo_power(i2c_master_bus_handle_t bus)
{
    i2c_master_dev_handle_t dev = nullptr;
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = PY32_ADDR,
        .scl_speed_hz    = 100000,
        .scl_wait_us     = 0,
        .flags           = {},
    };
    esp_err_t ret = i2c_master_bus_add_device(bus, &dev_cfg, &dev);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "PY32 IO expander add device failed: %s; servo power may already be enabled", esp_err_to_name(ret));
        return;
    }

    auto read_reg = [&](uint8_t reg, uint8_t& value) {
        return i2c_master_transmit_receive(dev, &reg, 1, &value, 1, pdMS_TO_TICKS(100));
    };
    auto write_reg = [&](uint8_t reg, uint8_t value) {
        uint8_t data[2] = {reg, value};
        return i2c_master_transmit(dev, data, sizeof(data), pdMS_TO_TICKS(100));
    };
    auto update_bit = [&](uint8_t reg, uint8_t bit_mask, bool set) {
        uint8_t value = 0;
        esp_err_t err = read_reg(reg, value);
        if (err != ESP_OK) {
            return err;
        }
        value = set ? (value | bit_mask) : (value & ~bit_mask);
        return write_reg(reg, value);
    };

    uint8_t version = 0;
    bool found      = false;
    for (int i = 0; i < 8; ++i) {
        if (read_reg(PY32_REG_VERSION, version) == ESP_OK && version != 0 && version != 0xff) {
            found = true;
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(150));
    }

    if (!found) {
        ESP_LOGW(TAG, "PY32 IO expander not found; servo power may already be enabled");
        i2c_master_bus_rm_device(dev);
        return;
    }

    ESP_LOGI(TAG, "PY32 IO expander version=0x%02x; enabling servo VM rail", version);
    ret = update_bit(PY32_REG_GPIO_M_L, 0x01, true);
    ret = ret == ESP_OK ? update_bit(PY32_REG_GPIO_PD_L, 0x01, false) : ret;
    ret = ret == ESP_OK ? update_bit(PY32_REG_GPIO_PU_L, 0x01, true) : ret;
    ret = ret == ESP_OK ? update_bit(PY32_REG_GPIO_O_L, 0x01, true) : ret;
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "failed to update PY32 servo power pin: %s", esp_err_to_name(ret));
    }
    i2c_master_bus_rm_device(dev);
    vTaskDelay(pdMS_TO_TICKS(200));
}

uint8_t scs_checksum(uint8_t id, uint8_t length, uint8_t instruction, const uint8_t* params, size_t param_len)
{
    uint16_t sum = id + length + instruction;
    for (size_t i = 0; i < param_len; ++i) {
        sum += params[i];
    }
    return static_cast<uint8_t>(~sum);
}

void scs_write_packet(uint8_t id, uint8_t instruction, const uint8_t* params, size_t param_len)
{
    uint8_t header[5] = {
        0xff,
        0xff,
        id,
        static_cast<uint8_t>(param_len + 2),
        instruction,
    };
    const uint8_t checksum = scs_checksum(id, header[3], instruction, params, param_len);

    uart_flush_input(SERVO_UART);
    uart_write_bytes(SERVO_UART, header, sizeof(header));
    if (param_len > 0) {
        uart_write_bytes(SERVO_UART, params, param_len);
    }
    uart_write_bytes(SERVO_UART, &checksum, 1);
    uart_wait_tx_done(SERVO_UART, pdMS_TO_TICKS(100));
}

bool scs_read_response(uint8_t expected_id, uint8_t* out_data, size_t out_max_len, size_t& out_len)
{
    out_len = 0;
    uint8_t prev = 0;
    uint8_t cur  = 0;
    int guard    = 0;
    while (guard++ < 24) {
        if (uart_read_bytes(SERVO_UART, &cur, 1, pdMS_TO_TICKS(20)) != 1) {
            return false;
        }
        if (prev == 0xff && cur == 0xff) {
            break;
        }
        prev = cur;
    }
    if (guard >= 24) {
        return false;
    }

    uint8_t hdr[3] = {};
    if (uart_read_bytes(SERVO_UART, hdr, sizeof(hdr), pdMS_TO_TICKS(20)) != sizeof(hdr)) {
        return false;
    }
    const uint8_t id      = hdr[0];
    const uint8_t length  = hdr[1];
    const uint8_t status  = hdr[2];
    const size_t data_len = length >= 2 ? static_cast<size_t>(length - 2) : 0;
    if (id != expected_id || data_len > out_max_len) {
        return false;
    }

    uint8_t checksum = 0;
    uint16_t sum     = id + length + status;
    if (data_len > 0) {
        if (uart_read_bytes(SERVO_UART, out_data, data_len, pdMS_TO_TICKS(20)) != static_cast<int>(data_len)) {
            return false;
        }
        for (size_t i = 0; i < data_len; ++i) {
            sum += out_data[i];
        }
    }
    if (uart_read_bytes(SERVO_UART, &checksum, 1, pdMS_TO_TICKS(20)) != 1) {
        return false;
    }
    if (checksum != static_cast<uint8_t>(~sum)) {
        return false;
    }
    out_len = data_len;
    return true;
}

bool scs_read_ack(uint8_t id)
{
    uint8_t data[1] = {};
    size_t data_len = 0;
    return scs_read_response(id, data, 0, data_len) && data_len == 0;
}

int scs_read_word(uint8_t id, uint8_t address)
{
    uint8_t params[2] = {address, 2};
    uint8_t data[4]   = {};
    size_t data_len   = 0;

    uart_flush_input(SERVO_UART);
    scs_write_packet(id, SCS_INST_READ, params, sizeof(params));
    if (!scs_read_response(id, data, sizeof(data), data_len) || data_len != 2) {
        return -1;
    }
    return (static_cast<int>(data[0]) << 8) | data[1];
}

int scs_read_byte(uint8_t id, uint8_t address)
{
    uint8_t params[2] = {address, 1};
    uint8_t data[4]   = {};
    size_t data_len   = 0;

    uart_flush_input(SERVO_UART);
    scs_write_packet(id, SCS_INST_READ, params, sizeof(params));
    if (!scs_read_response(id, data, sizeof(data), data_len) || data_len != 1) {
        return -1;
    }
    return data[0];
}

void log_servo_raw_positions(const char* label)
{
    const int yaw_present    = scs_read_word(SERVO_YAW_ID, SCS_PRESENT_POSITION_L);
    const int pitch_present  = scs_read_word(SERVO_PITCH_ID, SCS_PRESENT_POSITION_L);
    const int yaw_goal       = scs_read_word(SERVO_YAW_ID, SCS_GOAL_POSITION_L);
    const int pitch_goal     = scs_read_word(SERVO_PITCH_ID, SCS_GOAL_POSITION_L);
    const int yaw_torque     = scs_read_byte(SERVO_YAW_ID, SCS_TORQUE_ENABLE);
    const int pitch_torque   = scs_read_byte(SERVO_PITCH_ID, SCS_TORQUE_ENABLE);
    ESP_LOGI(TAG,
             "%s servo present=[%d,%d] goal=[%d,%d] torque=[%d,%d]",
             label,
             yaw_present,
             pitch_present,
             yaw_goal,
             pitch_goal,
             yaw_torque,
             pitch_torque);
}

void scs_write(uint8_t id, uint8_t address, const uint8_t* data, size_t data_len)
{
    uint8_t params[8] = {address};
    const size_t params_len = data_len + 1;
    if (params_len > sizeof(params)) {
        ESP_LOGW(TAG, "SCS write skipped, payload too large");
        return;
    }
    memcpy(params + 1, data, data_len);
    scs_write_packet(id, SCS_INST_WRITE, params, params_len);
    (void)scs_read_ack(id);
    vTaskDelay(pdMS_TO_TICKS(2));
}

void servo_enable_torque(uint8_t id, bool enabled)
{
    const uint8_t value = enabled ? 1 : 0;
    scs_write(id, SCS_TORQUE_ENABLE, &value, 1);
}

int servo_angle_to_raw(int angle, int zero_pos)
{
    const int raw = zero_pos + angle * 16 / 5 / 10;
    return std::clamp(raw, SERVO_RAW_MIN, SERVO_RAW_MAX);
}

void servo_write_pos(uint8_t id, int raw_position, int command_time, int speed)
{
    uint8_t data[6] = {
        static_cast<uint8_t>((raw_position >> 8) & 0xff),
        static_cast<uint8_t>(raw_position & 0xff),
        static_cast<uint8_t>((command_time >> 8) & 0xff),
        static_cast<uint8_t>(command_time & 0xff),
        static_cast<uint8_t>((speed >> 8) & 0xff),
        static_cast<uint8_t>(speed & 0xff),
    };
    scs_write(id, SCS_GOAL_POSITION_L, data, sizeof(data));
}

void servo_move_angles(int yaw_angle, int pitch_angle)
{
    yaw_angle   = std::clamp(yaw_angle, -SERVO_YAW_LIMIT, SERVO_YAW_LIMIT);
    pitch_angle = std::clamp(pitch_angle, SERVO_PITCH_MIN, SERVO_PITCH_MAX);

    servo_enable_torque(SERVO_YAW_ID, true);
    servo_enable_torque(SERVO_PITCH_ID, true);
    servo_write_pos(SERVO_YAW_ID, servo_angle_to_raw(yaw_angle, SERVO_YAW_ZERO_POS), SERVO_COMMAND_TIME, 0);
    servo_write_pos(SERVO_PITCH_ID, servo_angle_to_raw(pitch_angle, SERVO_PITCH_ZERO_POS), SERVO_COMMAND_TIME, 0);
}

void init_servo_bus()
{
    const uart_config_t uart_config = {
        .baud_rate           = SERVO_UART_BAUD,
        .data_bits           = UART_DATA_8_BITS,
        .parity              = UART_PARITY_DISABLE,
        .stop_bits           = UART_STOP_BITS_1,
        .flow_ctrl           = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 0,
        .source_clk          = UART_SCLK_DEFAULT,
        .flags               = {},
    };

    ESP_ERROR_CHECK(uart_driver_install(SERVO_UART, 1024, 1024, 0, nullptr, 0));
    ESP_ERROR_CHECK(uart_param_config(SERVO_UART, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(SERVO_UART, SERVO_UART_TX_PIN, SERVO_UART_RX_PIN, UART_PIN_NO_CHANGE,
                                 UART_PIN_NO_CHANGE));

    servo_move_angles(0, SERVO_PITCH_CENTER);
    ESP_LOGI(TAG, "servo bus ready; centered yaw=0 pitch=%d", SERVO_PITCH_CENTER);
    log_servo_raw_positions("after center");
}

void run_servo_self_test()
{
    ESP_LOGI(TAG, "servo self-test: yaw left/right then center");
    log_servo_raw_positions("before self-test");
    servo_move_angles(-280, SERVO_PITCH_CENTER);
    vTaskDelay(pdMS_TO_TICKS(350));
    log_servo_raw_positions("after yaw left");
    servo_move_angles(280, SERVO_PITCH_CENTER);
    vTaskDelay(pdMS_TO_TICKS(350));
    log_servo_raw_positions("after yaw right");
    servo_move_angles(0, SERVO_PITCH_CENTER);
    vTaskDelay(pdMS_TO_TICKS(250));
    log_servo_raw_positions("after self-test center");
    ESP_LOGI(TAG, "servo self-test done");
}

float apply_deadband(float value, float deadband)
{
    const float magnitude = fabsf(value);
    if (magnitude <= deadband) {
        return 0.0f;
    }
    const float scaled = (magnitude - deadband) / (1.0f - deadband);
    return value < 0.0f ? -scaled : scaled;
}

float face_width_servo_scale(const TrackingState& tracking)
{
    if (!tracking.box_valid) {
        return 1.0f;
    }
    const int width = std::max(1, tracking.box[2] - tracking.box[0] + 1);
    return std::clamp(SERVO_FACE_WIDTH_REF_PX / static_cast<float>(width),
                      SERVO_FACE_SCALE_MIN,
                      SERVO_FACE_SCALE_MAX);
}

int clamp_delta(float value, int max_step)
{
    const int delta = static_cast<int>(lroundf(value));
    if (delta > max_step) {
        return max_step;
    }
    if (delta < -max_step) {
        return -max_step;
    }
    return delta;
}

void update_servo_tracking(ServoState& servo, const TrackingState& tracking, int64_t now_us)
{
    if (!tracking.initialized || !has_recent_face_box(tracking, now_us)) {
        return;
    }
    if (servo.initialized && now_us - servo.last_cmd_us < SERVO_UPDATE_INTERVAL_MS * 1000LL) {
        return;
    }

    const float control_x = apply_deadband(tracking.x, SERVO_CENTER_DEADBAND_X);
    const float control_y = apply_deadband(tracking.y, SERVO_CENTER_DEADBAND_Y);
    if (control_x == 0.0f && control_y == 0.0f) {
        return;
    }

    const float face_scale = face_width_servo_scale(tracking);
    const int yaw_delta = clamp_delta(-control_x * SERVO_YAW_P_GAIN * face_scale, SERVO_MAX_YAW_STEP);
    const int pitch_delta = clamp_delta(-control_y * SERVO_PITCH_P_GAIN * face_scale, SERVO_MAX_PITCH_STEP);
    if (yaw_delta == 0 && pitch_delta == 0) {
        return;
    }

    const int current_yaw = servo.initialized ? servo.yaw_angle : 0;
    const int current_pitch = servo.initialized ? servo.pitch_angle : SERVO_PITCH_CENTER;
    const int command_yaw = std::clamp(current_yaw + yaw_delta, -SERVO_YAW_LIMIT, SERVO_YAW_LIMIT);
    const int command_pitch = std::clamp(current_pitch + pitch_delta, SERVO_PITCH_MIN, SERVO_PITCH_MAX);

    if (servo.initialized && std::abs(command_yaw - current_yaw) < SERVO_MIN_DELTA &&
        std::abs(command_pitch - current_pitch) < SERVO_MIN_DELTA) {
        return;
    }

    servo_move_angles(command_yaw, command_pitch);
    servo.yaw_angle   = command_yaw;
    servo.pitch_angle = command_pitch;
    servo.last_cmd_us = now_us;
    servo.initialized = true;

    const int face_age_ms = static_cast<int>((now_us - tracking.last_face_us) / 1000);
    ESP_LOGI(TAG,
             "servo step delta=[%d,%d] command=[%d,%d] raw=[%d,%d] err=(%.2f,%.2f) scale=%.2f face_age=%dms",
             yaw_delta,
             pitch_delta,
             command_yaw,
             command_pitch,
             servo_angle_to_raw(command_yaw, SERVO_YAW_ZERO_POS),
             servo_angle_to_raw(command_pitch, SERVO_PITCH_ZERO_POS),
             tracking.x,
             tracking.y,
             face_scale,
             face_age_ms);
    if (ENABLE_SERVO_TRACK_READBACK) {
        vTaskDelay(pdMS_TO_TICKS(120));
        log_servo_raw_positions("after tracking command");
    }
}

void servo_control_task(void* arg)
{
    (void)arg;
    ServoState servo;
    int64_t last_processed_update_us = 0;
    while (true) {
        TrackingState tracking;
        int64_t update_us = 0;
        if (read_tracking_state(tracking, update_us) && update_us != last_processed_update_us) {
            last_processed_update_us = update_us;
            update_servo_tracking(servo, tracking, esp_timer_get_time());
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void debug_display_task(void* arg)
{
    auto* display = static_cast<DebugDisplay*>(arg);
    if (display == nullptr || !display->initialized) {
        vTaskDelete(nullptr);
        return;
    }

    draw_debug_background(*display);
    while (true) {
        TrackingState tracking;
        int64_t update_us = 0;
        if (read_tracking_state(tracking, update_us)) {
            (void)update_us;
            update_debug_display_dot(*display, tracking, esp_timer_get_time());
        }
        vTaskDelay(pdMS_TO_TICKS(DISPLAY_UPDATE_INTERVAL_MS));
    }
}

bool dl_pix_type_from_v4l2(uint32_t format, dl::image::pix_type_t& pix_type)
{
    switch (format) {
        case V4L2_PIX_FMT_YUV422P:
        case V4L2_PIX_FMT_YUYV:
            pix_type = dl::image::DL_IMAGE_PIX_TYPE_YUYV;
            return true;
        case V4L2_PIX_FMT_RGB565:
            pix_type = dl::image::DL_IMAGE_PIX_TYPE_RGB565LE;
            return true;
        case V4L2_PIX_FMT_RGB565X:
            pix_type = dl::image::DL_IMAGE_PIX_TYPE_RGB565BE;
            return true;
        case V4L2_PIX_FMT_RGB24:
            pix_type = dl::image::DL_IMAGE_PIX_TYPE_RGB888;
            return true;
        case V4L2_PIX_FMT_GREY:
            pix_type = dl::image::DL_IMAGE_PIX_TYPE_GRAY;
            return true;
        default:
            return false;
    }
}

const char* dl_pix_type_name(dl::image::pix_type_t pix_type)
{
    switch (pix_type) {
        case dl::image::DL_IMAGE_PIX_TYPE_RGB565LE:
            return "RGB565LE";
        case dl::image::DL_IMAGE_PIX_TYPE_RGB565BE:
            return "RGB565BE";
        case dl::image::DL_IMAGE_PIX_TYPE_BGR565LE:
            return "BGR565LE";
        case dl::image::DL_IMAGE_PIX_TYPE_BGR565BE:
            return "BGR565BE";
        case dl::image::DL_IMAGE_PIX_TYPE_YUYV:
            return "YUYV";
        case dl::image::DL_IMAGE_PIX_TYPE_GRAY:
            return "GRAY";
        default:
            return "unknown";
    }
}

void set_detector_score_thr(HumanFaceDetect& detector, float threshold)
{
    detector.set_score_thr(threshold, 0);
    detector.set_score_thr(threshold, 1);
}

dl::image::pix_type_t tracking_pix_type_for_camera(const CameraState& camera, dl::image::pix_type_t detected_pix_type)
{
    if (camera.pixel_fmt == V4L2_PIX_FMT_RGB565) {
        ESP_LOGI(TAG, "tracking pixel format override: camera reports RGBP, color dump matches RGB565BE");
        return dl::image::DL_IMAGE_PIX_TYPE_RGB565BE;
    }
    return detected_pix_type;
}

esp_err_t init_video(i2c_master_bus_handle_t i2c_bus)
{
    static esp_cam_ctlr_dvp_pin_config_t dvp_pin_config = {
        .data_width = CAM_CTLR_DATA_WIDTH_8,
        .data_io =
            {
                [0] = CAMERA_PIN_D0,
                [1] = CAMERA_PIN_D1,
                [2] = CAMERA_PIN_D2,
                [3] = CAMERA_PIN_D3,
                [4] = CAMERA_PIN_D4,
                [5] = CAMERA_PIN_D5,
                [6] = CAMERA_PIN_D6,
                [7] = CAMERA_PIN_D7,
            },
        .vsync_io = CAMERA_PIN_VSYNC,
        .de_io    = CAMERA_PIN_HREF,
        .pclk_io  = CAMERA_PIN_PCLK,
        .xclk_io  = CAMERA_PIN_XCLK,
    };

    esp_video_init_sccb_config_t sccb_config = {
        .init_sccb  = false,
        .i2c_handle = i2c_bus,
        .freq       = 100000,
    };

    esp_video_init_dvp_config_t dvp_config = {
        .sccb_config = sccb_config,
        .reset_pin   = CAMERA_PIN_RESET,
        .pwdn_pin    = CAMERA_PIN_PWDN,
        .dvp_pin     = dvp_pin_config,
        .xclk_freq   = XCLK_FREQ_HZ,
    };

    esp_video_init_config_t video_config = {
        .dvp = &dvp_config,
    };

    return esp_video_init(&video_config);
}

uint32_t pick_format(int fd)
{
    struct v4l2_fmtdesc fmtdesc = {};
    fmtdesc.type                = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    uint32_t best_fmt           = 0;
    int best_rank               = 1 << 30;

    auto rank = [](uint32_t fmt) {
        switch (fmt) {
            case V4L2_PIX_FMT_YUV422P:
            case V4L2_PIX_FMT_YUYV:
                return 0;
            case V4L2_PIX_FMT_RGB565:
                return 1;
            case V4L2_PIX_FMT_RGB24:
                return 2;
            case V4L2_PIX_FMT_GREY:
                return 3;
            default:
                return 1 << 29;
        }
    };

    while (ioctl(fd, VIDIOC_ENUM_FMT, &fmtdesc) == 0) {
        char fourcc[5];
        ESP_LOGI(TAG, "format[%lu] %s %s", fmtdesc.index, fourcc_to_str(fmtdesc.pixelformat, fourcc),
                 fmtdesc.description);
        int r = rank(fmtdesc.pixelformat);
        if (r < best_rank) {
            best_rank = r;
            best_fmt  = fmtdesc.pixelformat;
        }
        fmtdesc.index++;
    }

    return best_fmt;
}

esp_err_t open_camera(CameraState& camera)
{
    camera.fd = open(ESP_VIDEO_DVP_DEVICE_NAME, O_RDWR);
    if (camera.fd < 0) {
        ESP_LOGE(TAG, "open %s failed errno=%d (%s)", ESP_VIDEO_DVP_DEVICE_NAME, errno, strerror(errno));
        return ESP_FAIL;
    }

    struct v4l2_capability cap = {};
    ESP_RETURN_ON_FALSE(ioctl(camera.fd, VIDIOC_QUERYCAP, &cap) == 0, ESP_FAIL, TAG, "VIDIOC_QUERYCAP failed");
    ESP_LOGI(TAG, "video driver=%s card=%s", cap.driver, cap.card);

    uint32_t fmt = pick_format(camera.fd);
    ESP_RETURN_ON_FALSE(fmt != 0, ESP_FAIL, TAG, "no supported pixel format");

    struct v4l2_format format = {};
    format.type               = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    format.fmt.pix.width      = 320;
    format.fmt.pix.height     = 240;
    format.fmt.pix.pixelformat = fmt;
    ESP_RETURN_ON_FALSE(ioctl(camera.fd, VIDIOC_S_FMT, &format) == 0, ESP_FAIL, TAG, "VIDIOC_S_FMT failed");

    camera.pixel_fmt = format.fmt.pix.pixelformat;
    camera.width     = format.fmt.pix.width;
    camera.height    = format.fmt.pix.height;
    char fourcc[5];
    ESP_LOGI(TAG, "capture %lux%lu %s", camera.width, camera.height, fourcc_to_str(camera.pixel_fmt, fourcc));

    struct v4l2_requestbuffers req = {};
    req.count                      = 2;
    req.type                       = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory                     = V4L2_MEMORY_MMAP;
    ESP_RETURN_ON_FALSE(ioctl(camera.fd, VIDIOC_REQBUFS, &req) == 0, ESP_FAIL, TAG, "VIDIOC_REQBUFS failed");
    camera.buffer_cnt = std::min<uint32_t>(req.count, 2);

    for (uint32_t i = 0; i < camera.buffer_cnt; ++i) {
        struct v4l2_buffer buf = {};
        buf.type               = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory             = V4L2_MEMORY_MMAP;
        buf.index              = i;
        ESP_RETURN_ON_FALSE(ioctl(camera.fd, VIDIOC_QUERYBUF, &buf) == 0, ESP_FAIL, TAG, "VIDIOC_QUERYBUF failed");
        void* start = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, camera.fd, buf.m.offset);
        ESP_RETURN_ON_FALSE(start != MAP_FAILED, ESP_FAIL, TAG, "mmap failed");
        camera.buffers[i].start  = start;
        camera.buffers[i].length = buf.length;
        ESP_RETURN_ON_FALSE(ioctl(camera.fd, VIDIOC_QBUF, &buf) == 0, ESP_FAIL, TAG, "VIDIOC_QBUF failed");
    }

    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ESP_RETURN_ON_FALSE(ioctl(camera.fd, VIDIOC_STREAMON, &type) == 0, ESP_FAIL, TAG, "VIDIOC_STREAMON failed");
    return ESP_OK;
}

void close_camera(CameraState& camera)
{
    if (camera.fd >= 0) {
        int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        ioctl(camera.fd, VIDIOC_STREAMOFF, &type);
    }
    for (uint32_t i = 0; i < camera.buffer_cnt; ++i) {
        if (camera.buffers[i].start && camera.buffers[i].length) {
            munmap(camera.buffers[i].start, camera.buffers[i].length);
        }
    }
    if (camera.fd >= 0) {
        close(camera.fd);
    }
}

void update_tracking(TrackingState& state, float raw_x, float raw_y, bool detected, float score)
{
    if (!detected) {
        if (state.initialized && state.lost_count < LOST_HOLD_DETECTIONS) {
            state.lost_count++;
            return;
        }
        if (state.initialized && state.lost_count < 1000) {
            state.lost_count++;
        }
        state.x *= LOST_DECAY;
        state.y *= LOST_DECAY;
        return;
    }

    if (!state.initialized) {
        state.x           = raw_x;
        state.y           = raw_y;
        state.initialized = true;
        state.lost_count  = 0;
        return;
    }

    state.lost_count = 0;
    float dx = raw_x - state.x;
    float dy = raw_y - state.y;
    if (fabsf(dx) < TRACK_MEASUREMENT_DEADBAND) {
        raw_x = state.x;
        dx    = 0.0f;
    }
    if (fabsf(dy) < TRACK_MEASUREMENT_DEADBAND) {
        raw_y = state.y;
        dy    = 0.0f;
    }
    const float distance = sqrtf(dx * dx + dy * dy);
    float alpha = SMOOTH_ALPHA;
    if (distance < TRACK_STILL_DISTANCE) {
        alpha = SMOOTH_ALPHA_STILL;
    }
    if (score < 0.55f) {
        alpha = SMOOTH_ALPHA_LOW_SCORE;
    }
    if (distance > TRACK_OUTLIER_DISTANCE) {
        alpha = SMOOTH_ALPHA_BIG_JUMP;
    }
    state.x = state.x * (1.0f - alpha) + raw_x * alpha;
    state.y = state.y * (1.0f - alpha) + raw_y * alpha;
}

void update_tracking_box(TrackingState& state, const int box[4], int64_t now_us, float raw_x, float raw_y)
{
    state.box_valid = true;
    state.last_face_us = now_us;
    state.last_face_x = raw_x;
    state.last_face_y = raw_y;
    state.box[0] = box[0];
    state.box[1] = box[1];
    state.box[2] = box[2];
    state.box[3] = box[3];
}

bool has_recent_face_box(const TrackingState& state, int64_t now_us)
{
    return state.box_valid && state.last_face_us > 0 &&
        now_us - state.last_face_us <= FAST_TRACK_MAX_AGE_MS * 1000LL;
}

bool sample_rgb_for_tracking(const CameraState& camera,
                             const uint8_t* data,
                             uint32_t x,
                             uint32_t y,
                             uint8_t& r,
                             uint8_t& g,
                             uint8_t& b)
{
    if (x >= camera.width || y >= camera.height) {
        return false;
    }

    switch (camera.pixel_fmt) {
        case V4L2_PIX_FMT_RGB565:
        case V4L2_PIX_FMT_RGB565X: {
            const size_t offset = (static_cast<size_t>(y) * camera.width + x) * 2;
            uint16_t pixel = 0;
            if (camera.pixel_fmt == V4L2_PIX_FMT_RGB565) {
                // This board reports RGB565, but the verified camera dump is byte-swapped.
                pixel = (static_cast<uint16_t>(data[offset]) << 8) | static_cast<uint16_t>(data[offset + 1]);
            } else {
                pixel = (static_cast<uint16_t>(data[offset]) << 8) | static_cast<uint16_t>(data[offset + 1]);
            }
            r = static_cast<uint8_t>(((pixel >> 11) & 0x1f) * 255 / 31);
            g = static_cast<uint8_t>(((pixel >> 5) & 0x3f) * 255 / 63);
            b = static_cast<uint8_t>((pixel & 0x1f) * 255 / 31);
            return true;
        }
        default:
            return false;
    }
}

bool looks_like_skin(uint8_t r, uint8_t g, uint8_t b)
{
    const int max_c = std::max(static_cast<int>(r), std::max(static_cast<int>(g), static_cast<int>(b)));
    const int min_c = std::min(static_cast<int>(r), std::min(static_cast<int>(g), static_cast<int>(b)));
    if (max_c - min_c < 12 || r < 45 || g < 25 || b < 15) {
        return false;
    }
    return r > g + 5 && r > b + 12 && g >= b - 8;
}

bool update_fast_skin_tracking(const CameraState& camera,
                               const uint8_t* frame_data,
                               TrackingState& tracking,
                               int64_t now_us,
                               float& raw_x,
                               float& raw_y,
                               int& skin_pixels,
                               int& total_samples)
{
    if (!tracking.initialized || !has_recent_face_box(tracking, now_us)) {
        if (tracking.box_valid && tracking.last_face_us > 0 &&
            now_us - tracking.last_face_us > FAST_TRACK_MAX_AGE_MS * 1000LL) {
            tracking.box_valid = false;
        }
        return false;
    }

    const int prev_w = std::max(12, tracking.box[2] - tracking.box[0] + 1);
    const int prev_h = std::max(12, tracking.box[3] - tracking.box[1] + 1);
    const int prev_cx = (tracking.box[0] + tracking.box[2]) / 2;
    const int prev_cy = (tracking.box[1] + tracking.box[3]) / 2;
    const int roi_w = static_cast<int>(lroundf(static_cast<float>(prev_w) * FAST_TRACK_ROI_SCALE));
    const int roi_h = static_cast<int>(lroundf(static_cast<float>(prev_h) * FAST_TRACK_ROI_SCALE));
    const int x0 = std::clamp(prev_cx - roi_w / 2, 0, static_cast<int>(camera.width) - 1);
    const int x1 = std::clamp(prev_cx + roi_w / 2, 0, static_cast<int>(camera.width) - 1);
    const int y0 = std::clamp(prev_cy - roi_h / 2, 0, static_cast<int>(camera.height) - 1);
    const int y1 = std::clamp(prev_cy + roi_h / 2, 0, static_cast<int>(camera.height) - 1);

    int64_t sum_x = 0;
    int64_t sum_y = 0;
    skin_pixels = 0;
    total_samples = 0;
    for (int y = y0; y <= y1; y += FAST_TRACK_SAMPLE_STEP) {
        for (int x = x0; x <= x1; x += FAST_TRACK_SAMPLE_STEP) {
            uint8_t r = 0;
            uint8_t g = 0;
            uint8_t b = 0;
            if (!sample_rgb_for_tracking(camera, frame_data, x, y, r, g, b)) {
                continue;
            }
            total_samples++;
            if (!looks_like_skin(r, g, b)) {
                continue;
            }
            const int weight = 1 + std::min<int>(12, (static_cast<int>(r) - static_cast<int>(b)) / 10);
            sum_x += static_cast<int64_t>(x) * weight;
            sum_y += static_cast<int64_t>(y) * weight;
            skin_pixels += weight;
        }
    }

    const int min_skin = std::max(FAST_TRACK_MIN_SKIN_PIXELS, total_samples / 24);
    if (skin_pixels < min_skin) {
        return false;
    }

    const float cx = static_cast<float>(sum_x) / static_cast<float>(skin_pixels);
    const float cy = static_cast<float>(sum_y) / static_cast<float>(skin_pixels);
    raw_x = clamp_unit((cx - (static_cast<float>(camera.width) - 1.0f) * 0.5f) /
                       ((static_cast<float>(camera.width) - 1.0f) * 0.5f));
    raw_y = clamp_unit((cy - (static_cast<float>(camera.height) - 1.0f) * 0.5f) /
                       ((static_cast<float>(camera.height) - 1.0f) * 0.5f));

    const float dx = raw_x - tracking.x;
    if (fabsf(raw_x - tracking.last_face_x) > FAST_TRACK_ANCHOR_DELTA) {
        return false;
    }
    if (fabsf(dx) < FAST_TRACK_X_DEADBAND) {
        return true;
    }
    if (fabsf(dx) > FAST_TRACK_MAX_X_DELTA) {
        return false;
    }

    tracking.x = tracking.x * (1.0f - FAST_TRACK_ALPHA) + raw_x * FAST_TRACK_ALPHA;
    return true;
}

bool is_trackable_face(const CameraState& camera,
                       const TrackingState& tracking,
                       float score,
                       const int box[4],
                       float raw_x,
                       float raw_y,
                       const char** reject_reason)
{
    const int width  = box[2] - box[0] + 1;
    const int height = box[3] - box[1] + 1;
    if (score < TRACK_SCORE_THR) {
        *reject_reason = "score";
        return false;
    }
    if (!tracking.initialized && score < FACE_INIT_SCORE_THR) {
        *reject_reason = "init_score";
        return false;
    }
    const bool touches_edge = box[0] < FACE_EDGE_MARGIN_PX || box[1] < FACE_EDGE_MARGIN_PX ||
        box[2] > static_cast<int>(camera.width) - 1 - FACE_EDGE_MARGIN_PX ||
        box[3] > static_cast<int>(camera.height) - 1 - FACE_EDGE_MARGIN_PX;
    if (touches_edge && score < FACE_EDGE_ACCEPT_SCORE) {
        *reject_reason = "edge";
        return false;
    }
    if (width <= 0 || height <= 0) {
        *reject_reason = "box";
        return false;
    }
    const float area = static_cast<float>(width * height);
    const float min_area = static_cast<float>(camera.width * camera.height) * FACE_MIN_AREA_RATIO;
    if (area < min_area) {
        *reject_reason = "small";
        return false;
    }
    const float aspect = static_cast<float>(width) / static_cast<float>(height);
    if (aspect < FACE_MIN_ASPECT || aspect > FACE_MAX_ASPECT) {
        *reject_reason = "aspect";
        return false;
    }
    if (tracking.initialized && tracking.lost_count <= TRACK_OUTLIER_HOLD_DETECTIONS) {
        const float dx = raw_x - tracking.x;
        const float dy = raw_y - tracking.y;
        const float distance = sqrtf(dx * dx + dy * dy);
        if (distance > TRACK_OUTLIER_DISTANCE && score < TRACK_OUTLIER_ACCEPT_SCORE) {
            *reject_reason = "jump";
            return false;
        }
    }

    *reject_reason = "ok";
    return true;
}

void log_detection_probe(HumanFaceDetect& detector,
                         const char* prefix,
                         const uint8_t* data,
                         uint16_t width,
                         uint16_t height,
                         dl::image::pix_type_t pix_type)
{
    dl::image::img_t img = {
        .data     = const_cast<uint8_t*>(data),
        .width    = width,
        .height   = height,
        .pix_type = pix_type,
    };

    int64_t start_us = esp_timer_get_time();
    auto& faces      = detector.run(img);
    int64_t infer_ms = (esp_timer_get_time() - start_us) / 1000;

    if (faces.empty()) {
        ESP_LOGI(TAG, "%s count=0 infer=%dms", prefix, static_cast<int>(infer_ms));
        return;
    }

    const auto* best = &faces.front();
    for (const auto& face : faces) {
        if (face.score > best->score) {
            best = &face;
        }
    }
    ESP_LOGI(TAG, "%s count=%u best_score=%.2f box=[%d,%d,%d,%d] infer=%dms", prefix,
             static_cast<unsigned>(faces.size()), best->score, best->box[0], best->box[1], best->box[2], best->box[3],
             static_cast<int>(infer_ms));
}

void probe_live_pixel_formats(HumanFaceDetect& detector,
                              const char* model_name,
                              const CameraState& camera,
                              const uint8_t* data,
                              bool low_threshold_probe)
{
    struct Variant {
        const char* name;
        dl::image::pix_type_t pix_type;
    };

    static constexpr Variant VARIANTS[] = {
        {"RGB565LE", dl::image::DL_IMAGE_PIX_TYPE_RGB565LE},
        {"RGB565BE", dl::image::DL_IMAGE_PIX_TYPE_RGB565BE},
        {"BGR565LE", dl::image::DL_IMAGE_PIX_TYPE_BGR565LE},
        {"BGR565BE", dl::image::DL_IMAGE_PIX_TYPE_BGR565BE},
    };

    const uint16_t width  = static_cast<uint16_t>(camera.width);
    const uint16_t height = static_cast<uint16_t>(camera.height);
    const float thresholds[] = {0.35f, low_threshold_probe ? 0.20f : 0.35f};
    const int threshold_count = low_threshold_probe ? 2 : 1;
    for (int i = 0; i < threshold_count; ++i) {
        float threshold = thresholds[i];
        detector.set_score_thr(threshold, 0);
        detector.set_score_thr(threshold, 1);
        for (const auto& variant : VARIANTS) {
            char label[64];
            snprintf(label, sizeof(label), "live probe model=%s thr=%.2f pix=%s", model_name, threshold, variant.name);
            log_detection_probe(detector, label, data, width, height, variant.pix_type);
        }
    }
    detector.set_score_thr(0.35f, 0);
    detector.set_score_thr(0.35f, 1);
}

void run_embedded_face_self_test()
{
    constexpr uint16_t TEST_W = 320;
    constexpr uint16_t TEST_H = 240;
    const size_t image_bytes = test_face_320x240_rgb565_end - test_face_320x240_rgb565_start;
    ESP_LOGI(TAG, "embedded face self-test image=%ux%u RGB565 bytes=%u", TEST_W, TEST_H,
             static_cast<unsigned>(image_bytes));
    if (image_bytes != TEST_W * TEST_H * 2) {
        ESP_LOGW(TAG, "embedded face self-test skipped due unexpected image size");
        return;
    }

    HumanFaceDetect detector(HumanFaceDetect::MSRMNP_S8_V1, true);
    detector.set_score_thr(0.35f, 0);
    detector.set_score_thr(0.35f, 1);

    dl::image::img_t img = {
        .data     = const_cast<uint8_t*>(test_face_320x240_rgb565_start),
        .width    = TEST_W,
        .height   = TEST_H,
        .pix_type = dl::image::DL_IMAGE_PIX_TYPE_RGB565LE,
    };

    int64_t start_us = esp_timer_get_time();
    auto& faces      = detector.run(img);
    int64_t infer_ms = (esp_timer_get_time() - start_us) / 1000;
    ESP_LOGI(TAG, "embedded face self-test result count=%u infer=%dms", static_cast<unsigned>(faces.size()),
             static_cast<int>(infer_ms));

    int idx = 0;
    for (const auto& face : faces) {
        ESP_LOGI(TAG, "embedded face[%d] score=%.2f box=[%d,%d,%d,%d]", idx, face.score, face.box[0], face.box[1],
                 face.box[2], face.box[3]);
        if (++idx >= 3) {
            break;
        }
    }

#if CONFIG_FLASH_ESPDET_PICO_224_224_FACE
    HumanFaceDetect espdet(HumanFaceDetect::ESPDET_PICO_224_224_FACE, true);
    espdet.set_score_thr(0.25f, 0);
    log_detection_probe(espdet,
                        "embedded face self-test model=ESPDET224 thr=0.25 pix=RGB565LE",
                        test_face_320x240_rgb565_start,
                        TEST_W,
                        TEST_H,
                        dl::image::DL_IMAGE_PIX_TYPE_RGB565LE);
#endif
}

void run_tracking_loop(CameraState& camera)
{
    dl::image::pix_type_t pix_type;
    if (!dl_pix_type_from_v4l2(camera.pixel_fmt, pix_type)) {
        char fourcc[5];
        ESP_LOGE(TAG, "unsupported detection pixel format %s", fourcc_to_str(camera.pixel_fmt, fourcc));
        return;
    }
    pix_type = tracking_pix_type_for_camera(camera, pix_type);

#if CONFIG_FLASH_ESPDET_PICO_224_224_FACE
    HumanFaceDetect detector(HumanFaceDetect::ESPDET_PICO_224_224_FACE, true);
    const char* detector_name = "ESPDET224";
#else
    HumanFaceDetect detector(HumanFaceDetect::MSRMNP_S8_V1, true);
    const char* detector_name = "MSRMNP";
#endif
    set_detector_score_thr(detector, DETECT_SCORE_THR);
    TrackingState tracking;
    int64_t last_detect_us = 0;
    int64_t last_stats_us  = esp_timer_get_time();
    uint32_t frames        = 0;
    uint32_t detects       = 0;
    uint32_t hits          = 0;
    uint32_t fast_hits     = 0;
    uint32_t fast_misses   = 0;
    bool frame_probe_logged = false;
    bool frame_dump_logged  = false;
    bool live_format_probe_logged = false;
    int64_t last_fast_log_us = 0;

    const size_t processing_frame_len = camera.buffer_cnt > 0 ? camera.buffers[0].length : 0;
    auto* processing_frame = static_cast<uint8_t*>(
        heap_caps_malloc(processing_frame_len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (processing_frame == nullptr) {
        processing_frame = static_cast<uint8_t*>(heap_caps_malloc(processing_frame_len, MALLOC_CAP_8BIT));
    }
    if (processing_frame == nullptr) {
        ESP_LOGE(TAG, "failed to allocate camera processing buffer len=%u", static_cast<unsigned>(processing_frame_len));
        return;
    }

    ESP_LOGI(TAG,
             "tracking loop started; detector=%s score_thr=%.2f pix=%s detect interval acquire=%dms track=%dms",
             detector_name,
             DETECT_SCORE_THR,
             dl_pix_type_name(pix_type),
             ACQUIRE_DETECT_INTERVAL_MS,
             DETECT_INTERVAL_MS);

    while (true) {
        struct v4l2_buffer buf = {};
        buf.type               = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory             = V4L2_MEMORY_MMAP;
        if (ioctl(camera.fd, VIDIOC_DQBUF, &buf) != 0) {
            ESP_LOGW(TAG, "VIDIOC_DQBUF failed errno=%d (%s)", errno, strerror(errno));
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        frames++;
        auto* mmap_frame_data = static_cast<uint8_t*>(camera.buffers[buf.index].start);
        const size_t copy_len = std::min(camera.buffers[buf.index].length, processing_frame_len);
        memcpy(processing_frame, mmap_frame_data, copy_len);
        if (ioctl(camera.fd, VIDIOC_QBUF, &buf) != 0) {
            ESP_LOGW(TAG, "VIDIOC_QBUF failed errno=%d (%s)", errno, strerror(errno));
        }
        auto* frame_data = processing_frame;
        if (ENABLE_FRAME_DIAGNOSTICS && !frame_probe_logged && frames >= 10) {
            log_frame_probe(camera, frame_data);
            frame_probe_logged = true;
        }
        if (ENABLE_FRAME_DIAGNOSTICS && !frame_dump_logged && frames >= 12) {
            dump_frame_pgm_hex(camera, frame_data);
            dump_frame_rgb565_hex(camera, frame_data);
            frame_dump_logged = true;
        }
        if (ENABLE_LIVE_FORMAT_PROBE && !live_format_probe_logged && frames >= 14) {
            HumanFaceDetect msrmnp(HumanFaceDetect::MSRMNP_S8_V1, true);
            set_detector_score_thr(msrmnp, DETECT_SCORE_THR);
            probe_live_pixel_formats(msrmnp, "MSRMNP", camera, frame_data, true);
#if CONFIG_FLASH_ESPDET_PICO_224_224_FACE
            HumanFaceDetect espdet(HumanFaceDetect::ESPDET_PICO_224_224_FACE, true);
            set_detector_score_thr(espdet, DETECT_SCORE_THR);
            probe_live_pixel_formats(espdet, "ESPDET224", camera, frame_data, false);
#endif
            live_format_probe_logged = true;
        }
        bool did_detect = false;
        bool found      = false;
        float raw_x     = 0.0f;
        float raw_y     = 0.0f;
        float score     = 0.0f;
        int box[4]      = {0, 0, 0, 0};
        uint32_t faces_seen = 0;
        const char* reject_reason = "none";
        int64_t now_us  = esp_timer_get_time();
        int64_t infer_ms = 0;

        const int active_detect_interval_ms =
            has_recent_face_box(tracking, now_us) ? DETECT_INTERVAL_MS : ACQUIRE_DETECT_INTERVAL_MS;
        if (now_us - last_detect_us >= active_detect_interval_ms * 1000LL) {
            last_detect_us = now_us;
            did_detect     = true;
            detects++;

            dl::image::img_t img = {
                .data     = frame_data,
                .width    = static_cast<uint16_t>(camera.width),
                .height   = static_cast<uint16_t>(camera.height),
                .pix_type = pix_type,
            };

            int64_t start_us = esp_timer_get_time();
            auto& faces      = detector.run(img);
            infer_ms         = (esp_timer_get_time() - start_us) / 1000;

            if (!faces.empty()) {
                faces_seen = static_cast<uint32_t>(faces.size());
                float best_rank = -1000.0f;
                for (const auto& face : faces) {
                    int candidate_box[4] = {face.box[0], face.box[1], face.box[2], face.box[3]};
                    float cx = (static_cast<float>(candidate_box[0] + candidate_box[2]) * 0.5f);
                    float cy = (static_cast<float>(candidate_box[1] + candidate_box[3]) * 0.5f);
                    float candidate_raw_x =
                        clamp_unit((cx - (static_cast<float>(camera.width) - 1.0f) * 0.5f) /
                                   ((static_cast<float>(camera.width) - 1.0f) * 0.5f));
                    float candidate_raw_y =
                        clamp_unit((cy - (static_cast<float>(camera.height) - 1.0f) * 0.5f) /
                                   ((static_cast<float>(camera.height) - 1.0f) * 0.5f));

                    const char* candidate_reject = "none";
                    if (!is_trackable_face(camera,
                                           tracking,
                                           face.score,
                                           candidate_box,
                                           candidate_raw_x,
                                           candidate_raw_y,
                                           &candidate_reject)) {
                        reject_reason = candidate_reject;
                        continue;
                    }

                    float continuity_penalty = 0.0f;
                    if (tracking.initialized) {
                        continuity_penalty = FACE_CONTINUITY_PENALTY * (fabsf(candidate_raw_x - tracking.x) +
                                                                         fabsf(candidate_raw_y - tracking.y));
                    }
                    const float rank = face.score - continuity_penalty;
                    if (!found || rank > best_rank) {
                        best_rank = rank;
                        score     = face.score;
                        box[0]    = candidate_box[0];
                        box[1]    = candidate_box[1];
                        box[2]    = candidate_box[2];
                        box[3]    = candidate_box[3];
                        raw_x     = candidate_raw_x;
                        raw_y     = candidate_raw_y;
                        found     = true;
                    }
                }
                if (found) {
                    hits++;
                }
            }

            update_tracking(tracking, raw_x, raw_y, found, score);
            if (found) {
                update_tracking_box(tracking, box, now_us, raw_x, raw_y);
            }
        } else {
            int skin_pixels = 0;
            int total_samples = 0;
            float fast_raw_x = 0.0f;
            float fast_raw_y = 0.0f;
            bool fast_found = update_fast_skin_tracking(
                camera, frame_data, tracking, now_us, fast_raw_x, fast_raw_y, skin_pixels, total_samples);
            if (fast_found) {
                fast_hits++;
                int64_t log_now_us = esp_timer_get_time();
                if (log_now_us - last_fast_log_us >= FAST_TRACK_LOG_INTERVAL_MS * 1000LL) {
                    const int face_age_ms = static_cast<int>((log_now_us - tracking.last_face_us) / 1000);
                    ESP_LOGI(TAG,
                             "fast track yaw-only skin=%d/%d raw=(%.2f,%.2f) smooth=(%.2f,%.2f) face_age=%dms box=[%d,%d,%d,%d]",
                             skin_pixels,
                             total_samples,
                             fast_raw_x,
                             fast_raw_y,
                             tracking.x,
                             tracking.y,
                             face_age_ms,
                             tracking.box[0],
                             tracking.box[1],
                             tracking.box[2],
                             tracking.box[3]);
                    last_fast_log_us = log_now_us;
                }
            } else {
                fast_misses++;
            }
        }

        publish_tracking_state(tracking, esp_timer_get_time());

        if (did_detect) {
            if (found) {
                ESP_LOGI(TAG,
                         "face hit faces=%u score=%.2f box=[%d,%d,%d,%d] raw=(%.2f,%.2f) smooth=(%.2f,%.2f) lost=%d infer=%dms heap=%u psram=%u",
                         static_cast<unsigned>(faces_seen), score, box[0], box[1], box[2], box[3], raw_x, raw_y,
                         tracking.x, tracking.y, tracking.lost_count,
                         static_cast<int>(infer_ms),
                         static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
                         static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
            } else {
                ESP_LOGI(TAG, "face miss faces=%u reject=%s smooth=(%.2f,%.2f) lost=%d infer=%dms heap=%u psram=%u",
                         static_cast<unsigned>(faces_seen), reject_reason, tracking.x, tracking.y, tracking.lost_count,
                         static_cast<int>(infer_ms),
                         static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
                         static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
            }
        }

        int64_t stats_now_us = esp_timer_get_time();
        if (stats_now_us - last_stats_us >= 5000000LL) {
            float seconds = static_cast<float>(stats_now_us - last_stats_us) / 1000000.0f;
            ESP_LOGI(TAG,
                     "stats frames=%.1ffps detects=%.1f/s hit_rate=%.0f%% fast=%.1f/s fast_hit=%u/%u smooth=(%.2f,%.2f)",
                     static_cast<float>(frames) / seconds, static_cast<float>(detects) / seconds,
                     detects ? (static_cast<float>(hits) * 100.0f / static_cast<float>(detects)) : 0.0f,
                     static_cast<float>(fast_hits + fast_misses) / seconds,
                     static_cast<unsigned>(fast_hits),
                     static_cast<unsigned>(fast_hits + fast_misses),
                     tracking.x,
                     tracking.y);
            frames        = 0;
            detects       = 0;
            hits          = 0;
            fast_hits     = 0;
            fast_misses   = 0;
            last_stats_us = stats_now_us;
        }

        vTaskDelay(1);
    }
}

}  // namespace

extern "C" void app_main(void)
{
    esp_log_level_set(TAG, ESP_LOG_INFO);
    g_tracking_mutex = xSemaphoreCreateMutex();
    if (g_tracking_mutex == nullptr) {
        ESP_LOGE(TAG, "tracking state mutex allocation failed");
        return;
    }

    run_embedded_face_self_test();

    i2c_master_bus_handle_t i2c_bus = nullptr;
    ESP_ERROR_CHECK(init_i2c(&i2c_bus));
    init_axp2101_minimal(i2c_bus);
    vTaskDelay(pdMS_TO_TICKS(200));
    if (!init_aw9523_minimal(i2c_bus)) {
        ESP_LOGE(TAG, "camera board GPIO init failed; stop demo without reboot loop");
        return;
    }

    if (init_debug_display(i2c_bus, g_debug_display) == ESP_OK) {
        BaseType_t display_task_ret = xTaskCreatePinnedToCore(
            debug_display_task, "debug_display", 4096, &g_debug_display, 1, nullptr, 1);
        if (display_task_ret != pdPASS) {
            ESP_LOGW(TAG, "debug display task create failed");
        }
    } else {
        ESP_LOGW(TAG, "debug display disabled");
    }

    esp_err_t video_ret = ESP_FAIL;
    for (int attempt = 1; attempt <= 5; ++attempt) {
        video_ret = init_video(i2c_bus);
        if (video_ret == ESP_OK) {
            break;
        }
        ESP_LOGW(TAG, "init_video attempt %d failed: %s", attempt, esp_err_to_name(video_ret));
        vTaskDelay(pdMS_TO_TICKS(250));
    }
    if (video_ret != ESP_OK) {
        ESP_LOGE(TAG, "camera init failed; stop demo without reboot loop");
        return;
    }

    CameraState camera;
    esp_err_t camera_ret = ESP_FAIL;
    for (int attempt = 1; attempt <= 5; ++attempt) {
        camera_ret = open_camera(camera);
        if (camera_ret == ESP_OK) {
            break;
        }
        ESP_LOGW(TAG, "open_camera attempt %d failed: %s", attempt, esp_err_to_name(camera_ret));
        close_camera(camera);
        vTaskDelay(pdMS_TO_TICKS(250));
    }
    if (camera_ret != ESP_OK) {
        ESP_LOGE(TAG, "camera open failed; stop demo without reboot loop");
        return;
    }

    init_servo_power(i2c_bus);
    init_servo_bus();
    if (ENABLE_SERVO_SELF_TEST) {
        run_servo_self_test();
    }
    BaseType_t servo_task_ret =
        xTaskCreatePinnedToCore(servo_control_task, "servo_control", 4096, nullptr, 5, nullptr, 1);
    if (servo_task_ret != pdPASS) {
        ESP_LOGW(TAG, "servo control task create failed; tracking will run without servo motion");
    }

    run_tracking_loop(camera);
    close_camera(camera);
}
