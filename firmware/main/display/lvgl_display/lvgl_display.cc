#include "lvgl_display.h"

#include "application.h"
#include "assets/lang_config.h"
#include "audio_codec.h"
#include "board.h"
#include "jpg/image_to_jpeg.h"

#include <esp_err.h>
#include <esp_log.h>
#include <font_awesome.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <ctime>

namespace {

constexpr const char* kTag = "LvglDisplay";
constexpr int kClockRefreshSeconds = 10;
constexpr int kLowBatteryLevel = 0;

void DeleteObject(lv_obj_t*& object)
{
    if (object == nullptr) {
        return;
    }
    lv_obj_del(object);
    object = nullptr;
}

void StopAndDeleteTimer(esp_timer_handle_t& timer)
{
    if (timer == nullptr) {
        return;
    }
    esp_timer_stop(timer);
    esp_timer_delete(timer);
    timer = nullptr;
}

bool SetHidden(lv_obj_t* object, bool hidden)
{
    if (object == nullptr) {
        return false;
    }
    if (hidden) {
        lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(object, LV_OBJ_FLAG_HIDDEN);
    }
    return true;
}

class PmLockScope {
public:
    explicit PmLockScope(esp_pm_lock_handle_t lock) : lock_(lock)
    {
        if (lock_ != nullptr && esp_pm_lock_acquire(lock_) == ESP_OK) {
            acquired_ = true;
        }
    }

    ~PmLockScope()
    {
        if (acquired_) {
            esp_pm_lock_release(lock_);
        }
    }

private:
    esp_pm_lock_handle_t lock_ = nullptr;
    bool acquired_ = false;
};

class LvglDrawBuffer {
public:
    explicit LvglDrawBuffer(lv_draw_buf_t* buffer) : buffer_(buffer) {}
    ~LvglDrawBuffer()
    {
        if (buffer_ != nullptr) {
            lv_draw_buf_destroy(buffer_);
        }
    }

    lv_draw_buf_t* get() const { return buffer_; }

private:
    lv_draw_buf_t* buffer_ = nullptr;
};

const char* BatteryIcon(int level, bool charging)
{
    if (charging) {
        return FONT_AWESOME_BATTERY_BOLT;
    }

    static constexpr std::array<const char*, 6> kIcons = {
        FONT_AWESOME_BATTERY_EMPTY,
        FONT_AWESOME_BATTERY_QUARTER,
        FONT_AWESOME_BATTERY_HALF,
        FONT_AWESOME_BATTERY_THREE_QUARTERS,
        FONT_AWESOME_BATTERY_FULL,
        FONT_AWESOME_BATTERY_FULL,
    };

    const int clamped_level = std::clamp(level, 0, 100);
    return kIcons[clamped_level / 20];
}

bool SameIcon(const char* lhs, const char* rhs)
{
    if (lhs == rhs) {
        return true;
    }
    if (lhs == nullptr || rhs == nullptr) {
        return false;
    }
    return std::strcmp(lhs, rhs) == 0;
}

bool TimeIsSet(const tm& local_time)
{
    return local_time.tm_year >= 2025 - 1900;
}

}  // namespace

LvglDisplay::LvglDisplay()
{
    const esp_timer_create_args_t timer_args = {
        .callback =
            [](void* arg) {
                auto* display = static_cast<LvglDisplay*>(arg);
                DisplayLockGuard lock(display);
                SetHidden(display->notification_label_, true);
                SetHidden(display->status_label_, false);
            },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "notification_timer",
        .skip_unhandled_events = false,
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &notification_timer_));

    const esp_err_t err = esp_pm_lock_create(ESP_PM_APB_FREQ_MAX, 0, "display_update", &pm_lock_);
    if (err == ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGI(kTag, "Power management locks are not available");
    } else if (err != ESP_OK) {
        ESP_LOGW(kTag, "Failed to create display PM lock: %s", esp_err_to_name(err));
    }
}

LvglDisplay::~LvglDisplay()
{
    StopAndDeleteTimer(notification_timer_);
    DeleteObject(network_label_);
    DeleteObject(notification_label_);
    DeleteObject(status_label_);
    DeleteObject(mute_label_);
    DeleteObject(battery_label_);
    DeleteObject(low_battery_popup_);

    if (pm_lock_ != nullptr) {
        esp_pm_lock_delete(pm_lock_);
        pm_lock_ = nullptr;
    }
}

void LvglDisplay::SetStatus(const char* status)
{
    const char* safe_status = status != nullptr ? status : "";
    DisplayLockGuard lock(this);
    if (status_label_ == nullptr) {
        if (setup_ui_called_) {
            ESP_LOGW(kTag, "Cannot show status before the status label exists");
        }
        return;
    }

    lv_label_set_text(status_label_, safe_status);
    SetHidden(status_label_, false);
    SetHidden(notification_label_, true);
    last_status_update_time_ = std::chrono::system_clock::now();
}

void LvglDisplay::ShowNotification(const std::string& notification, int duration_ms)
{
    ShowNotification(notification.c_str(), duration_ms);
}

void LvglDisplay::ShowNotification(const char* notification, int duration_ms)
{
    const char* safe_notification = notification != nullptr ? notification : "";
    DisplayLockGuard lock(this);
    if (notification_label_ == nullptr) {
        if (setup_ui_called_) {
            ESP_LOGW(kTag, "Cannot show notification before the notification label exists");
        }
        return;
    }

    lv_label_set_text(notification_label_, safe_notification);
    SetHidden(notification_label_, false);
    SetHidden(status_label_, true);

    if (duration_ms > 0) {
        esp_timer_stop(notification_timer_);
        ESP_ERROR_CHECK(esp_timer_start_once(notification_timer_, static_cast<uint64_t>(duration_ms) * 1000));
    }
}

void LvglDisplay::UpdateStatusBar(bool update_all)
{
    Board& board = Board::GetInstance();
    Application& app = Application::GetInstance();
    AudioCodec* codec = board.GetAudioCodec();

    {
        DisplayLockGuard lock(this);
        if (mute_label_ == nullptr || codec == nullptr) {
            return;
        }

        const bool now_muted = codec->output_volume() == 0;
        if (now_muted != muted_) {
            muted_ = now_muted;
            lv_label_set_text(mute_label_, muted_ ? FONT_AWESOME_VOLUME_XMARK : "");
        }
    }

    if (app.GetDeviceState() == kDeviceStateIdle &&
        last_status_update_time_ + std::chrono::seconds(kClockRefreshSeconds) < std::chrono::system_clock::now()) {
        const time_t now = time(nullptr);
        tm local_time = {};
        if (localtime_r(&now, &local_time) != nullptr && TimeIsSet(local_time)) {
            char clock[16] = {};
            if (strftime(clock, sizeof(clock), "%H:%M", &local_time) > 0) {
                SetStatus(clock);
            }
        }
    }

    PmLockScope pm_lock(pm_lock_);

    int battery_level = 0;
    bool charging = false;
    bool discharging = false;
    if (board.GetBatteryLevel(battery_level, charging, discharging)) {
        const char* icon = BatteryIcon(battery_level, charging);
        DisplayLockGuard lock(this);
        if (battery_label_ != nullptr && !SameIcon(battery_icon_, icon)) {
            battery_icon_ = icon;
            lv_label_set_text(battery_label_, icon);
        }

        if (low_battery_popup_ != nullptr && !update_all) {
            const bool low_battery = std::clamp(battery_level, 0, 100) <= kLowBatteryLevel && discharging;
            const bool popup_hidden = lv_obj_has_flag(low_battery_popup_, LV_OBJ_FLAG_HIDDEN);
            if (low_battery && popup_hidden) {
                SetHidden(low_battery_popup_, false);
                app.Schedule([&app]() {
                    app.PlaySound(Lang::Sounds::OGG_LOW_BATTERY);
                });
            } else if (!low_battery && !popup_hidden) {
                SetHidden(low_battery_popup_, true);
            }
        }
    }

    static int refresh_counter = 0;
    if (!update_all && refresh_counter++ % kClockRefreshSeconds != 0) {
        return;
    }

    const DeviceState state = app.GetDeviceState();
    const bool should_update_network =
        state == kDeviceStateIdle ||
        state == kDeviceStateStarting ||
        state == kDeviceStateWifiConfiguring ||
        state == kDeviceStateListening ||
        state == kDeviceStateActivating;
    if (!should_update_network) {
        return;
    }

    const char* icon = board.GetNetworkStateIcon();
    DisplayLockGuard lock(this);
    if (network_label_ != nullptr && icon != nullptr && !SameIcon(network_icon_, icon)) {
        network_icon_ = icon;
        lv_label_set_text(network_label_, icon);
    }
}

void LvglDisplay::SetPreviewImage(std::unique_ptr<LvglImage> image)
{
    (void)image;
}

void LvglDisplay::SetPowerSaveMode(bool on)
{
    SetChatMessage("system", "");
    SetEmotion(on ? "sleepy" : "neutral");
}

bool LvglDisplay::SnapshotToJpeg(std::string& jpeg_data, int quality)
{
#if CONFIG_LV_USE_SNAPSHOT
    DisplayLockGuard lock(this);

    LvglDrawBuffer snapshot(lv_snapshot_take(lv_screen_active(), LV_COLOR_FORMAT_RGB565));
    lv_draw_buf_t* buffer = snapshot.get();
    if (buffer == nullptr || buffer->data == nullptr || buffer->data_size == 0) {
        ESP_LOGE(kTag, "Failed to capture LVGL snapshot");
        return false;
    }

    uint16_t* pixels = reinterpret_cast<uint16_t*>(buffer->data);
    const size_t pixel_count = buffer->data_size / sizeof(uint16_t);
    for (size_t i = 0; i < pixel_count; ++i) {
        pixels[i] = __builtin_bswap16(pixels[i]);
    }

    jpeg_data.clear();
    const bool encoded = image_to_jpeg_cb(
        static_cast<uint8_t*>(buffer->data),
        buffer->data_size,
        buffer->header.w,
        buffer->header.h,
        V4L2_PIX_FMT_RGB565,
        quality,
        [](void* arg, size_t index, const void* data, size_t len) -> size_t {
            (void)index;
            auto* output = static_cast<std::string*>(arg);
            if (data != nullptr && len > 0) {
                output->append(static_cast<const char*>(data), len);
            }
            return len;
        },
        &jpeg_data);
    if (!encoded) {
        jpeg_data.clear();
        ESP_LOGE(kTag, "Failed to encode LVGL snapshot as JPEG");
    }
    return encoded;
#else
    (void)jpeg_data;
    (void)quality;
    ESP_LOGE(kTag, "LVGL snapshot support is disabled");
    return false;
#endif
}
