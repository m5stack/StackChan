/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_music_player.h"

#define MINIMP3_IMPLEMENTATION
#define MINIMP3_NO_SIMD
#include "minimp3.h"

#include <hal/hal.h>
#include <hal/board/config.h>
#include <mooncake.h>
#include <mooncake_log.h>
#include <stackchan/stackchan.h>
#include <apps/common/common.h>
#include <assets/assets.h>
#include <board.h>
#include <audio/audio_codec.h>
#include <settings.h>

#include <esp_heap_caps.h>
#include <esp_vfs_fat.h>
#include <sdmmc_cmd.h>
#include <driver/sdspi_host.h>
#include <driver/spi_common.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <map>
#include <sys/stat.h>

using namespace mooncake;
using namespace stackchan;

LV_IMAGE_DECLARE(icon_music);

namespace {

constexpr const char* TAG        = "MUSIC";
constexpr const char* APP_VERSION = "v4.0";
constexpr const char* TRACK_DIR  = "/sd";

// NVS keys (single namespace for all player preferences and last track)
constexpr const char* NVS_NS         = "music_player";
constexpr const char* KEY_Y_HOME     = "y_home";
constexpr const char* KEY_VOLUME     = "volume";
constexpr const char* KEY_LOOP_MODE  = "loop_mode";
constexpr const char* KEY_SHUFFLE    = "shuffle";
constexpr const char* KEY_LIGHTS     = "lights";
constexpr const char* KEY_MOVE       = "move";
constexpr const char* KEY_LAST_IDX   = "last_idx";
constexpr const char* KEY_LAST_PATH  = "last_path";

constexpr int W                  = 320;
constexpr int H                  = 240;
constexpr uint32_t BEAT_MS       = 600;
constexpr int VOLUME_STEP        = 10;
constexpr int X_HOME_DEG         = 0;
constexpr int Y_HOME_DEFAULT_DEG = 45;
constexpr int X_SWAY_DEG         = 6;
constexpr int Y_SWAY_DEG         = 5;
constexpr int SWAY_STILL_PCT     = 15;
constexpr int HEAD_STEP_DEG      = 5;

constexpr int MAX_TRACKS         = 2000;
constexpr int PAGE_ROWS          = 5;
constexpr int MAX_SCAN_DEPTH     = 3;

// Dark UI color palette
constexpr uint32_t COL_BG        = 0x0B0F1A;
constexpr uint32_t COL_CARD      = 0x151B2E;
constexpr uint32_t COL_CARD_HI   = 0x232C4A;
constexpr uint32_t COL_ACCENT    = 0x3D7DFF;
constexpr uint32_t COL_ACCENT_DK = 0x24417F;
constexpr uint32_t COL_YELLOW    = 0xFFC940;
constexpr uint32_t COL_TEXT      = 0xE8EEF9;
constexpr uint32_t COL_DIM       = 0x7C89A8;
constexpr uint32_t COL_WHITE     = 0xFFFFFF;
constexpr uint32_t COL_RED       = 0xE74C3C;
constexpr uint32_t COL_GREEN     = 0x34C759;
constexpr uint32_t COL_PLAY_ON   = 0x1F6B3A;

constexpr uint32_t PALETTE[8] = {
    0xFF3B30, 0xFF9500, 0xFFCC00, 0x34C759,
    0x00C7BE, 0x007AFF, 0x5856D6, 0xAF52DE,
};

// Top status/progress geometry
constexpr int TITLE_Y  = 4;
constexpr int BAR_X    = 8;
constexpr int BAR_Y    = 26;
constexpr int BAR_W    = 304;
constexpr int BAR_H    = 6;
constexpr int TIME_Y   = 35;
constexpr int STATUS_Y = 49;

// Center stage area (y = 64 .. 184)
constexpr int STAGE_X  = 8;
constexpr int STAGE_Y  = 64;
constexpr int STAGE_W  = 304;
constexpr int STAGE_H  = 120;

// Bottom 6-button control bar (y = 190 .. 232)
constexpr int BAR6_Y   = 190;
constexpr int BAR6_H   = 42;
constexpr int BAR6_W   = 48;
constexpr int BAR6_GAP = 4;
constexpr int BAR6_X0  = 6; // 6 + 6*48 + 5*4 = 314 <= 320

const char* const LOOP_NAMES[3] = {"OFF", "1", "ALL"};

const char* const MAIN_BTN_NAMES[6] = {
    "voldn", "prev", "play", "next", "volup", "settings",
};

struct CfgBtnSpec {
    const char* name;
    int x;
    int y;
    int w;
    int h;
};

// Settings screen layout:
// Row 1: LOOP | SHUFFLE | LIGHTS | MOVE
// Row 2: PLAYLIST | HEAD | EXIT
constexpr CfgBtnSpec CFG_BTN_SPECS[7] = {
    {"loop",     8,   48,  73, 56},
    {"shuf",     85,  48,  73, 56},
    {"lights",   162, 48,  73, 56},
    {"move",     239, 48,  73, 56},
    {"playlist", 8,   116, 98, 52},
    {"head",     111, 116, 98, 52},
    {"menu",     214, 116, 98, 52},
};

uint16_t read_u16_le(const uint8_t* p)
{
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

uint32_t read_u32_le(const uint8_t* p)
{
    return static_cast<uint32_t>(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
}

int py_y_deg_to_pitch(int y_deg)
{
    return (45 - y_deg) * 10;
}

int pitch_to_py_y_deg(int pitch_tenths)
{
    int deg = 45 - (pitch_tenths / 10);
    return std::clamp(deg, 0, 90);
}

int py_x_deg_to_yaw(int x_deg)
{
    return x_deg * 10;
}

void style_flat_box(lv_obj_t* obj, uint32_t bg_hex, int radius = 0)
{
    lv_obj_set_style_bg_color(obj, lv_color_hex(bg_hex), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_shadow_width(obj, 0, 0);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

}  // namespace

/* -------------------------------------------------------------------------- */
/*                           Mooncake Lifecycle                               */
/* -------------------------------------------------------------------------- */

AppMusicPlayer::AppMusicPlayer()
{
    setAppInfo().name = "MUSIC";

    static lv_image_dsc_t icon_from_assets = assets::get_image("icon_music.bin");
    if (icon_from_assets.data != nullptr && icon_from_assets.data_size > 0) {
        setAppInfo().icon = (void*)&icon_from_assets;
    } else {
        setAppInfo().icon = (void*)&icon_music;
    }

    static uint32_t theme_color = 0x3D7DFF;
    setAppInfo().userData       = (void*)&theme_color;
}

AppMusicPlayer::~AppMusicPlayer()
{
    stop_audio_task();
    unmount_sd();
}

void AppMusicPlayer::onCreate()
{
    mclog::tagInfo(getAppInfo().name, "on create");
}

void AppMusicPlayer::onOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");

    _mode                 = ScreenMode::Main;
    _status_msg.clear();
    _audio_err.clear();
    _last_beat            = 0xFFFFFFFF;
    _playing              = false;
    _started              = false;
    _track_eof            = false;
    _elapsed_ms           = 0;
    _duration_ms          = 0;
    _byte_rate            = 0;
    _decoded_bytes        = 0;

    // 1. Load saved preferences and last played track from NVS
    load_saved_prefs();
    GetHAL().setSpeakerVolume(static_cast<uint8_t>(_volume), false);

    // 2. Initialize servos and LEDs
    GetHAL().setServoPowerEnabled(true);
    rgb_off();
    auto& stackchan = GetStackChan();
    stackchan.clearModifiers();
    stackchan.motion().setAutoAngleSyncEnabled(false);
    stackchan.motion().setAutoTorqueReleaseEnabled(false);
    move_home(600);

    // 3. Mount microSD card and scan tracks (.wav and .mp3)
    _sd_mounted = mount_sd();
    scan_tracks();

    // 4. Restore last played track (by exact path, falling back to last index)
    if (!_tracks.empty()) {
        int restored = -1;
        if (!_saved_track_path.empty()) {
            for (int i = 0; i < static_cast<int>(_tracks.size()); ++i) {
                if (_tracks[i].path.size() == _saved_track_path.size() &&
                    std::memcmp(_tracks[i].path.c_str(), _saved_track_path.c_str(), _saved_track_path.size()) == 0) {
                    restored = i;
                    break;
                }
            }
        }
        if (restored < 0 && _saved_track_idx >= 0 && _saved_track_idx < static_cast<int>(_tracks.size())) {
            restored = _saved_track_idx;
        }
        _index     = (restored >= 0) ? restored : 0;
        _list_page = _index / PAGE_ROWS;

        auto info    = probe_audio(std::string(_tracks[_index].path.c_str()), _tracks[_index].is_mp3);
        _duration_ms = info.duration_ms;
        _byte_rate   = info.byte_rate;
    }

    // 5. Start audio decoding task (stack in PSRAM)
    start_audio_task();

    // 6. Build LVGL UI
    {
        LvglLockGuard lock;
        build_ui();
        uint32_t now = GetHAL().millis();
        refresh_main_ui(now);
        refresh_avatar_anim(now);

        view::create_home_indicator([&]() { close(); }, 0x3D7DFF, 0x0B0F1A);
        view::create_status_bar(0x3D7DFF, 0x0B0F1A);
    }
}

void AppMusicPlayer::onRunning()
{
    uint32_t now = GetHAL().millis();

    if (!_status_msg.empty() && static_cast<int32_t>(now - _status_until) >= 0) {
        _status_msg.clear();
        LvglLockGuard lock;
        refresh_main_ui(now);
    }

    if (_track_eof.exchange(false)) {
        track_finished();
        LvglLockGuard lock;
        refresh_main_ui(now);
        if (_mode == ScreenMode::List) {
            refresh_list_ui();
        }
    }

    beat_update(now);
    GetStackChan().update();

    {
        LvglLockGuard lock;

        if (_mode == ScreenMode::Main) {
            if (now - _last_anim_tick >= 120) {
                _last_anim_tick = now;
                refresh_avatar_anim(now);
            }
            if (now - _last_time_tick >= 500) {
                _last_time_tick = now;
                refresh_main_ui(now);
            }
            if (now - _last_batt_tick >= 5000) {
                _last_batt_tick = now;
                refresh_battery_ui();
            }
        } else if (_mode == ScreenMode::Head) {
            if (now - _last_time_tick >= 300) {
                _last_time_tick = now;
                refresh_head_ui();
            }
        }

        view::update_home_indicator();
        view::update_status_bar();
    }
}

void AppMusicPlayer::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");

    save_settings();
    save_last_track();

    stop_playback();
    stop_audio_task();

    move_home(500);
    rgb_off();

    {
        LvglLockGuard lock;
        view::destroy_home_indicator();
        view::destroy_status_bar();
        destroy_ui();
    }

    unmount_sd();

    GetHAL().requestWarmReboot(6);
}

/* -------------------------------------------------------------------------- */
/*                               microSD Card                                 */
/* -------------------------------------------------------------------------- */

bool AppMusicPlayer::mount_sd()
{
    struct stat st = {};
    if (stat(TRACK_DIR, &st) == 0) {
        _sd_mounted = true;
        return true;
    }

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {};
    mount_config.format_if_mount_failed           = false;
    mount_config.max_files                        = 5;
    mount_config.allocation_unit_size             = 16 * 1024;

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot         = SPI3_HOST;
    host.max_freq_khz = 20000;

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs               = GPIO_NUM_4;
    slot_config.host_id               = SPI3_HOST;

    sdmmc_card_t* card = nullptr;
    esp_err_t ret      = esp_vfs_fat_sdspi_mount(TRACK_DIR, &host, &slot_config, &mount_config, &card);
    if (ret != ESP_OK) {
        mclog::tagWarn(TAG, "SD mount failed on SPI3_HOST: {}", esp_err_to_name(ret));
        _scan_first_err = esp_err_to_name(ret);
        _sd_mounted     = false;
        _sd_card_handle = nullptr;
        return false;
    }

    _sd_card_handle = card;
    _sd_mounted     = true;
    mclog::tagInfo(TAG, "SD mounted at /sd");
    return true;
}

void AppMusicPlayer::unmount_sd()
{
    if (_sd_card_handle != nullptr) {
        esp_vfs_fat_sdcard_unmount(TRACK_DIR, static_cast<sdmmc_card_t*>(_sd_card_handle));
        _sd_card_handle = nullptr;
    }
    _sd_mounted = false;
}

bool AppMusicPlayer::is_audio_name(const std::string& name)
{
    if (name.size() <= 4 || name[0] == '.') {
        return false;
    }
    std::string ext = ext_of(name);
    return ext == ".wav" || ext == ".mp3";
}

std::string AppMusicPlayer::ext_of(const std::string& name)
{
    auto dot = name.find_last_of('.');
    if (dot == std::string::npos || dot + 1 >= name.size()) {
        return "";
    }
    std::string ext = name.substr(dot);
    for (char& c : ext) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return ext;
}

std::string AppMusicPlayer::clip_text(const std::string& s, size_t max_chars)
{
    if (s.size() <= max_chars) {
        return s;
    }
    return s.substr(0, max_chars);
}

std::string AppMusicPlayer::base_title(const std::string& path)
{
    auto slash       = path.find_last_of('/');
    std::string name = (slash == std::string::npos) ? path : path.substr(slash + 1);
    if (name.size() > 4) {
        std::string ext = ext_of(name);
        if (ext == ".wav" || ext == ".mp3") {
            name = name.substr(0, name.size() - 4);
        }
    }
    for (char& c : name) {
        if (c == '_') {
            c = ' ';
        }
    }
    return clip_text(name, 24);
}

std::string AppMusicPlayer::fmt_time(int32_t ms)
{
    if (ms < 0) {
        ms = 0;
    }
    int32_t s = ms / 1000;
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%d:%02d", static_cast<int>(s / 60), static_cast<int>(s % 60));
    return std::string(buf);
}

void AppMusicPlayer::scan_tracks()
{
    _tracks.clear();
    _scan_error   = false;
    _scan_entries = 0;
    _scan_other_ext.clear();
    _scan_other_n = 0;

    if (!_sd_mounted) {
        return;
    }

    std::map<std::string, int> exts;
    std::vector<std::pair<std::string, int>> stack;
    stack.push_back({TRACK_DIR, 0});

    while (!stack.empty() && static_cast<int>(_tracks.size()) < MAX_TRACKS) {
        auto [dir_path, depth] = stack.back();
        stack.pop_back();

        DIR* dir = opendir(dir_path.c_str());
        if (!dir) {
            if (depth == 0) {
                _scan_error = true;
            }
            continue;
        }

        struct dirent* ent = nullptr;
        while ((ent = readdir(dir)) != nullptr) {
            const char* dname = ent->d_name;
            if (!dname || dname[0] == '\0' || std::strcmp(dname, ".") == 0 || std::strcmp(dname, "..") == 0) {
                continue;
            }
            if (dname[0] == '.') {
                continue;
            }

            _scan_entries++;
            std::string name(dname);
            std::string full_path = dir_path + "/" + name;

            if (is_audio_name(name)) {
                if (static_cast<int>(_tracks.size()) < MAX_TRACKS) {
                    TrackEntry te;
                    te.path.assign(full_path.c_str(), full_path.size());
                    std::string title_tmp = base_title(full_path);
                    te.title.assign(title_tmp.c_str(), title_tmp.size());
                    te.is_mp3 = (ext_of(name) == ".mp3");
                    _tracks.push_back(std::move(te));
                }
                continue;
            }

            bool is_dir = (ent->d_type == DT_DIR);
            if (ent->d_type == DT_UNKNOWN) {
                struct stat st = {};
                if (stat(full_path.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
                    is_dir = true;
                }
            }

            if (is_dir) {
                if (depth < MAX_SCAN_DEPTH) {
                    stack.push_back({full_path, depth + 1});
                }
            } else {
                std::string e = ext_of(name);
                if (!e.empty() && e != ".mp3" && e != ".wav") {
                    exts[e]++;
                }
            }
        }
        closedir(dir);
    }

    std::sort(_tracks.begin(), _tracks.end(), [](const TrackEntry& a, const TrackEntry& b) {
        return a.path < b.path;
    });

    for (const auto& kv : exts) {
        if (kv.second > _scan_other_n) {
            _scan_other_n   = kv.second;
            _scan_other_ext = kv.first;
        }
    }

    mclog::tagInfo(TAG, "SD entries: {}, tracks: {}", _scan_entries, _tracks.size());
}

/* -------------------------------------------------------------------------- */
/*                         WAV / MP3 Probing & Audio                          */
/* -------------------------------------------------------------------------- */

AppMusicPlayer::AudioProbeInfo AppMusicPlayer::probe_wav(const std::string& path)
{
    AudioProbeInfo info;
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        return info;
    }

    uint8_t head[12];
    if (std::fread(head, 1, 12, f) < 12 || std::memcmp(head, "RIFF", 4) != 0 || std::memcmp(head + 8, "WAVE", 4) != 0) {
        std::fclose(f);
        return info;
    }

    while (true) {
        uint8_t ch[8];
        if (std::fread(ch, 1, 8, f) < 8) {
            break;
        }
        uint32_t size = read_u32_le(ch + 4);
        if (std::memcmp(ch, "fmt ", 4) == 0) {
            uint8_t data[32] = {};
            size_t to_read   = std::min<size_t>(size, sizeof(data));
            if (std::fread(data, 1, to_read, f) >= 16) {
                info.format_tag  = read_u16_le(data + 0);
                info.channels    = read_u16_le(data + 2);
                info.sample_rate = read_u32_le(data + 4);
                info.byte_rate   = read_u32_le(data + 8);
                info.bits        = read_u16_le(data + 14);
            }
            if (size > to_read) {
                std::fseek(f, static_cast<long>(size - to_read), SEEK_CUR);
            }
            if (size % 2 != 0) {
                std::fseek(f, 1, SEEK_CUR);
            }
        } else if (std::memcmp(ch, "data", 4) == 0) {
            info.data_offset = static_cast<uint32_t>(std::ftell(f));
            info.data_size   = size;
            break;
        } else {
            std::fseek(f, static_cast<long>(size + (size % 2)), SEEK_CUR);
        }
    }

    std::fclose(f);

    if (info.byte_rate > 0 && info.data_size > 0 && info.format_tag == 1 && info.channels >= 1) {
        info.duration_ms = static_cast<uint32_t>((static_cast<uint64_t>(info.data_size) * 1000ULL) / info.byte_rate);
        info.valid       = true;
    }
    return info;
}

AppMusicPlayer::AudioProbeInfo AppMusicPlayer::probe_mp3(const std::string& path)
{
    AudioProbeInfo info;
    struct stat st = {};
    if (stat(path.c_str(), &st) != 0 || st.st_size <= 0) {
        return info;
    }
    uint32_t file_size = static_cast<uint32_t>(st.st_size);

    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        return info;
    }

    uint8_t head[10] = {};
    uint32_t start   = 0;
    if (std::fread(head, 1, 10, f) == 10 && std::memcmp(head, "ID3", 3) == 0) {
        uint32_t tag = ((head[6] & 0x7F) << 21) | ((head[7] & 0x7F) << 14) | ((head[8] & 0x7F) << 7) | (head[9] & 0x7F);
        start        = 10 + tag;
    }

    std::fseek(f, static_cast<long>(start), SEEK_SET);
    std::vector<uint8_t> chunk(4096);
    size_t nread = std::fread(chunk.data(), 1, chunk.size(), f);
    std::fclose(f);

    info.data_offset = start;
    info.data_size   = (file_size > start) ? (file_size - start) : file_size;
    info.valid       = true;

    for (size_t i = 0; i + 4 <= nread; ++i) {
        if (chunk[i] == 0xFF && (chunk[i + 1] & 0xE0) == 0xE0) {
            uint8_t b1     = chunk[i + 1];
            uint8_t b2     = chunk[i + 2];
            uint8_t ver    = (b1 >> 3) & 0x03;
            uint8_t layer  = (b1 >> 1) & 0x03;
            uint8_t br_idx = (b2 >> 4) & 0x0F;
            if (layer == 1 && br_idx > 0 && br_idx < 15) {
                static const int table_v1[16] = {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0};
                static const int table_v2[16] = {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0};
                int kbps                      = (ver == 3) ? table_v1[br_idx] : table_v2[br_idx];
                if (kbps > 0) {
                    info.byte_rate   = static_cast<uint32_t>(kbps * 125);
                    info.duration_ms = static_cast<uint32_t>((static_cast<uint64_t>(info.data_size) * 1000ULL) / info.byte_rate);
                    break;
                }
            }
        }
    }

    return info;
}

AppMusicPlayer::AudioProbeInfo AppMusicPlayer::probe_audio(const std::string& path, bool is_mp3)
{
    return is_mp3 ? probe_mp3(path) : probe_wav(path);
}

void AppMusicPlayer::start_audio_task()
{
    if (_task_running.load()) {
        return;
    }
    _task_running = true;
    _task_exited  = false;
    BaseType_t ok = xTaskCreatePinnedToCoreWithCaps(audio_task_entry, "music_audio", 32768, this, 5, nullptr, 0,
                                                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ok != pdPASS) {
        mclog::tagWarn(TAG, "PSRAM audio task failed, falling back to internal RAM (20KB)");
        ok = xTaskCreatePinnedToCore(audio_task_entry, "music_audio", 20480, this, 5, nullptr, 0);
    }
    if (ok != pdPASS) {
        mclog::tagError(TAG, "Failed to create audio task");
        _task_running = false;
        _task_exited  = true;
    }
}

void AppMusicPlayer::stop_audio_task()
{
    if (!_task_running.load()) {
        return;
    }
    _playing      = false;
    _task_running = false;
    for (int i = 0; i < 100 && !_task_exited.load(); ++i) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void AppMusicPlayer::audio_task_entry(void* arg)
{
    static_cast<AppMusicPlayer*>(arg)->audio_task_loop();
    vTaskDelete(nullptr);
}

void AppMusicPlayer::audio_task_loop()
{
    auto& board      = Board::GetInstance();
    auto audio_codec = board.GetAudioCodec();

    FILE* file = nullptr;
    AudioProbeInfo probe;
    bool is_mp3 = false;

    mp3dec_t* mp3d = static_cast<mp3dec_t*>(heap_caps_malloc(sizeof(mp3dec_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!mp3d) {
        mp3d = static_cast<mp3dec_t*>(std::malloc(sizeof(mp3dec_t)));
    }
    if (mp3d) {
        mp3dec_init(mp3d);
    }

    constexpr size_t MP3_BUF_SIZE = 16384;
    uint8_t* mp3_buf = static_cast<uint8_t*>(heap_caps_malloc(MP3_BUF_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!mp3_buf) {
        mp3_buf = static_cast<uint8_t*>(std::malloc(MP3_BUF_SIZE));
    }
    int16_t* mp3_pcm = static_cast<int16_t*>(
        heap_caps_malloc(MINIMP3_MAX_SAMPLES_PER_FRAME * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!mp3_pcm) {
        mp3_pcm = static_cast<int16_t*>(std::malloc(MINIMP3_MAX_SAMPLES_PER_FRAME * sizeof(int16_t)));
    }
    uint8_t* wav_raw = static_cast<uint8_t*>(heap_caps_malloc(512 * 4, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    int16_t* wav_pcm = static_cast<int16_t*>(heap_caps_malloc(512 * 2 * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    size_t mp3_buf_filled = 0;

    std::vector<int16_t> out_24k;
    out_24k.reserve(2048);
    uint32_t played_out_samples = 0;
    int32_t base_start_ms       = 0;

    auto close_file = [&]() {
        if (file) {
            std::fclose(file);
            file = nullptr;
        }
        mp3_buf_filled = 0;
    };

    auto resample_and_output = [&](const int16_t* pcm, int frames, int channels, uint32_t src_rate) {
        if (!audio_codec || frames <= 0 || src_rate == 0) {
            return;
        }
        if (!audio_codec->output_enabled()) {
            if (audio_codec->input_enabled()) {
                audio_codec->EnableInput(false);
            }
            audio_codec->EnableOutput(true);
        }

        const uint32_t dst_rate = AUDIO_OUTPUT_SAMPLE_RATE;
        int out_frames          = static_cast<int>((static_cast<uint64_t>(frames) * dst_rate) / src_rate);
        if (out_frames <= 0) {
            return;
        }
        out_24k.resize(out_frames);

        for (int i = 0; i < out_frames; ++i) {
            uint64_t src_pos_fp = (static_cast<uint64_t>(i) * src_rate << 12) / dst_rate;
            int idx0            = static_cast<int>(src_pos_fp >> 12);
            int frac            = static_cast<int>(src_pos_fp & 0xFFF);
            if (idx0 >= frames) {
                idx0 = frames - 1;
            }
            int idx1 = (idx0 + 1 < frames) ? (idx0 + 1) : idx0;

            int32_t s0 = 0;
            int32_t s1 = 0;
            if (channels >= 2) {
                s0 = (static_cast<int32_t>(pcm[idx0 * channels]) + static_cast<int32_t>(pcm[idx0 * channels + 1])) / 2;
                s1 = (static_cast<int32_t>(pcm[idx1 * channels]) + static_cast<int32_t>(pcm[idx1 * channels + 1])) / 2;
            } else {
                s0 = pcm[idx0];
                s1 = pcm[idx1];
            }
            int32_t interp = s0 + (((s1 - s0) * frac) >> 12);
            out_24k[i]     = static_cast<int16_t>(std::clamp<int32_t>(interp, -32768, 32767));
        }

        audio_codec->OutputData(out_24k);
        played_out_samples += static_cast<uint32_t>(out_frames);
        int32_t cur_ms = base_start_ms + static_cast<int32_t>((static_cast<uint64_t>(played_out_samples) * 1000ULL) / dst_rate);
        _elapsed_ms.store(cur_ms);
    };

    while (_task_running.load()) {
        if (_cmd_reload.exchange(false)) {
            close_file();
            std::string path;
            int32_t start_ms = 0;
            {
                std::lock_guard<std::mutex> lk(_audio_mutex);
                path     = _active_path;
                is_mp3   = _active_is_mp3;
                start_ms = _target_start_ms;
                probe    = _active_probe;
            }

            if (!path.empty() && probe.valid) {
                file = std::fopen(path.c_str(), "rb");
                if (file) {
                    uint32_t seek_byte = probe.data_offset;
                    if (start_ms > 0 && probe.byte_rate > 0) {
                        uint32_t offset = static_cast<uint32_t>((static_cast<uint64_t>(start_ms) * probe.byte_rate) / 1000ULL);
                        if (!is_mp3) {
                            uint32_t frame_align = std::max<uint32_t>(1, (probe.channels * (probe.bits / 8)));
                            offset               = (offset / frame_align) * frame_align;
                        }
                        if (offset < probe.data_size) {
                            seek_byte += offset;
                        }
                    }
                    std::fseek(file, static_cast<long>(seek_byte), SEEK_SET);
                    _decoded_bytes.store(seek_byte - probe.data_offset);
                    base_start_ms      = start_ms;
                    played_out_samples = 0;
                    _elapsed_ms.store(start_ms);
                    if (mp3d) {
                        mp3dec_init(mp3d);
                    }
                }
            }
        }

        if (!_playing.load() || !file) {
            vTaskDelay(pdMS_TO_TICKS(15));
            continue;
        }

        if (!is_mp3) {
            constexpr int WAV_CHUNK_FRAMES = 512;
            int bytes_per_sample           = std::max<int>(1, probe.bits / 8);
            int frame_bytes                = bytes_per_sample * std::max<int>(1, probe.channels);
            uint32_t cur_rel               = _decoded_bytes.load();
            if (cur_rel >= probe.data_size) {
                close_file();
                _playing.store(false);
                _track_eof.store(true);
                continue;
            }

            uint32_t rem_bytes = probe.data_size - cur_rel;
            size_t to_read     = std::min<size_t>(WAV_CHUNK_FRAMES * frame_bytes, rem_bytes);
            if (!wav_raw || !wav_pcm) {
                close_file();
                _playing.store(false);
                continue;
            }
            if (to_read > 512 * 4) {
                to_read = 512 * 4;
            }
            size_t n = std::fread(wav_raw, 1, to_read, file);
            if (n < static_cast<size_t>(frame_bytes)) {
                close_file();
                _playing.store(false);
                _track_eof.store(true);
                continue;
            }

            _decoded_bytes.fetch_add(static_cast<uint32_t>(n));
            int frames_read = static_cast<int>(n / frame_bytes);

            if (probe.bits == 16) {
                std::memcpy(wav_pcm, wav_raw, frames_read * frame_bytes);
            } else if (probe.bits == 8) {
                int total_samples = frames_read * probe.channels;
                for (int i = 0; i < total_samples; ++i) {
                    wav_pcm[i] = static_cast<int16_t>((static_cast<int>(wav_raw[i]) - 128) << 8);
                }
            } else {
                close_file();
                _playing.store(false);
                _track_eof.store(true);
                continue;
            }

            resample_and_output(wav_pcm, frames_read, probe.channels, probe.sample_rate);
        } else {
            if (!mp3d || !mp3_buf || !mp3_pcm) {
                close_file();
                _playing.store(false);
                continue;
            }

            if (mp3_buf_filled < 4096) {
                size_t space = MP3_BUF_SIZE - mp3_buf_filled;
                size_t n     = std::fread(mp3_buf + mp3_buf_filled, 1, space, file);
                mp3_buf_filled += n;
            }

            if (mp3_buf_filled == 0) {
                close_file();
                _playing.store(false);
                _track_eof.store(true);
                continue;
            }

            mp3dec_frame_info_t frame_info = {};
            int samples = mp3dec_decode_frame(mp3d, mp3_buf, static_cast<int>(mp3_buf_filled), mp3_pcm, &frame_info);

            if (frame_info.frame_bytes > 0) {
                size_t consumed = static_cast<size_t>(frame_info.frame_bytes);
                if (consumed < mp3_buf_filled) {
                    std::memmove(mp3_buf, mp3_buf + consumed, mp3_buf_filled - consumed);
                    mp3_buf_filled -= consumed;
                } else {
                    mp3_buf_filled = 0;
                }
                _decoded_bytes.fetch_add(static_cast<uint32_t>(consumed));
            } else {
                if (std::feof(file)) {
                    close_file();
                    _playing.store(false);
                    _track_eof.store(true);
                    continue;
                }
                mp3_buf_filled = 0;
                continue;
            }

            if (samples > 0 && frame_info.hz > 0 && frame_info.channels > 0) {
                resample_and_output(mp3_pcm, samples, frame_info.channels, static_cast<uint32_t>(frame_info.hz));
            }
        }
    }

    close_file();
    if (audio_codec && audio_codec->output_enabled()) {
        audio_codec->EnableOutput(false);
    }
    if (mp3d) {
        heap_caps_free(mp3d);
    }
    if (mp3_buf) {
        heap_caps_free(mp3_buf);
    }
    if (mp3_pcm) {
        heap_caps_free(mp3_pcm);
    }
    if (wav_raw) {
        heap_caps_free(wav_raw);
    }
    if (wav_pcm) {
        heap_caps_free(wav_pcm);
    }
    _task_exited = true;
}

/* -------------------------------------------------------------------------- */
/*                            Playback Controls                               */
/* -------------------------------------------------------------------------- */

void AppMusicPlayer::play_track(int idx, int32_t start_ms)
{
    if (_tracks.empty()) {
        return;
    }

    int n  = static_cast<int>(_tracks.size());
    _index = ((idx % n) + n) % n;

    const auto& tr = _tracks[_index];
    std::string tr_path(tr.path.c_str(), tr.path.size());
    auto info = probe_audio(tr_path, tr.is_mp3);
    if (!info.valid) {
        set_status(tr.is_mp3 ? "INVALID MP3" : "INVALID WAV");
        _playing = false;
        _started = false;
        return;
    }

    if (start_ms < 0) {
        start_ms = 0;
    }
    if (info.duration_ms > 0 && start_ms > static_cast<int32_t>(info.duration_ms)) {
        start_ms = static_cast<int32_t>(info.duration_ms);
    }

    _duration_ms.store(static_cast<int32_t>(info.duration_ms));
    _byte_rate.store(info.byte_rate);
    _elapsed_ms.store(start_ms);

    {
        std::lock_guard<std::mutex> lk(_audio_mutex);
        _active_path     = tr_path;
        _active_is_mp3   = tr.is_mp3;
        _target_start_ms = start_ms;
        _active_probe    = info;
    }

    _track_eof  = false;
    _cmd_reload = true;
    _playing    = true;
    _started    = true;
    _status_msg.clear();
    _audio_err.clear();
    _last_beat = 0xFFFFFFFF;

    // Persist last played track immediately
    save_last_track();
}

void AppMusicPlayer::toggle_play()
{
    if (_tracks.empty()) {
        return;
    }

    if (_playing.load()) {
        _playing = false;
        if (_robot_move) {
            move_home(500);
        }
        if (_lights_on) {
            set_all_rgb(0x001028);
        } else {
            rgb_off();
        }
    } else {
        int32_t dur = _duration_ms.load();
        int32_t el  = _elapsed_ms.load();
        if (!_started.load() || (dur > 0 && el >= dur - 60)) {
            play_track(_index, 0);
            return;
        }
        _playing   = true;
        _last_beat = 0xFFFFFFFF;
    }
}

void AppMusicPlayer::stop_playback()
{
    _playing = false;
    _started = false;
}

void AppMusicPlayer::seek_to(int32_t ms)
{
    if (_tracks.empty()) {
        return;
    }
    if (_byte_rate.load() == 0) {
        set_status("SEEK UNAVAILABLE");
        return;
    }
    if (ms < 0) {
        ms = 0;
    }
    int32_t dur = _duration_ms.load();
    if (dur > 250 && ms > dur - 250) {
        ms = dur - 250;
    }
    play_track(_index, ms);
}

void AppMusicPlayer::change_track(int delta)
{
    if (_tracks.empty()) {
        return;
    }
    if (_shuffle_on && _tracks.size() > 1) {
        play_track(random_index(), 0);
    } else {
        int n = static_cast<int>(_tracks.size());
        play_track((_index + delta + n) % n, 0);
    }
}

void AppMusicPlayer::change_volume(int delta)
{
    _volume = std::clamp(_volume + delta, 0, 100);
    GetHAL().setSpeakerVolume(static_cast<uint8_t>(_volume), false);
    save_settings();
    set_status("VOL " + std::to_string(_volume));
}

void AppMusicPlayer::cycle_loop()
{
    _loop_mode = (_loop_mode + 1) % 3;
    save_settings();
    set_status(std::string("LOOP ") + LOOP_NAMES[_loop_mode]);
}

void AppMusicPlayer::toggle_move()
{
    _robot_move = !_robot_move;
    save_settings();
    if (_robot_move) {
        _last_beat = 0xFFFFFFFF;
        set_status("MOTION ON");
    } else {
        move_home(500);
        set_status("MOTION OFF");
    }
}

void AppMusicPlayer::toggle_lights()
{
    _lights_on = !_lights_on;
    save_settings();
    _last_beat = 0xFFFFFFFF;
    if (_lights_on) {
        set_status("LIGHTS ON");
    } else {
        rgb_off();
        set_status("LIGHTS OFF");
    }
}

void AppMusicPlayer::toggle_shuffle()
{
    _shuffle_on = !_shuffle_on;
    save_settings();
    set_status(std::string("SHUFFLE ") + (_shuffle_on ? "ON" : "OFF"));
}

int AppMusicPlayer::random_index() const
{
    int count = static_cast<int>(_tracks.size());
    if (count <= 1) {
        return 0;
    }
    int n = _index;
    for (int t = 0; t < 6 && n == _index; ++t) {
        n = std::rand() % count;
    }
    if (n == _index) {
        n = (_index + 1) % count;
    }
    return n;
}

void AppMusicPlayer::track_finished()
{
    int count = static_cast<int>(_tracks.size());
    if (_loop_mode == 1) {
        play_track(_index, 0);
    } else if (_shuffle_on && count > 1) {
        play_track(random_index(), 0);
    } else if (_index + 1 < count) {
        play_track(_index + 1, 0);
    } else if (_loop_mode == 2 && count > 0) {
        play_track(0, 0);
    } else {
        _playing    = false;
        _started    = false;
        _elapsed_ms = _duration_ms.load();
        rgb_off();
        if (_robot_move) {
            move_home(500);
        }
    }
}

/* -------------------------------------------------------------------------- */
/*                     Robot: Servos, LEDs & Persistence                      */
/* -------------------------------------------------------------------------- */

void AppMusicPlayer::load_saved_prefs()
{
    Settings settings(NVS_NS, false);

    int saved_y = settings.GetInt(KEY_Y_HOME, Y_HOME_DEFAULT_DEG);
    _y_home_deg = std::clamp(saved_y, 0, 90);

    int saved_vol = settings.GetInt(KEY_VOLUME, -1);
    _volume       = (saved_vol >= 0 && saved_vol <= 100) ? saved_vol : GetHAL().getSpeakerVolume();

    int saved_loop = settings.GetInt(KEY_LOOP_MODE, 0);
    _loop_mode     = (saved_loop >= 0 && saved_loop <= 2) ? saved_loop : 0;

    _shuffle_on = settings.GetBool(KEY_SHUFFLE, false);
    _lights_on  = settings.GetBool(KEY_LIGHTS, true);
    _robot_move = settings.GetBool(KEY_MOVE, true);

    _saved_track_idx  = settings.GetInt(KEY_LAST_IDX, 0);
    _saved_track_path = settings.GetString(KEY_LAST_PATH, "");
}

void AppMusicPlayer::save_settings()
{
    Settings settings(NVS_NS, true);
    settings.SetInt(KEY_VOLUME, _volume);
    settings.SetInt(KEY_LOOP_MODE, _loop_mode);
    settings.SetBool(KEY_SHUFFLE, _shuffle_on);
    settings.SetBool(KEY_LIGHTS, _lights_on);
    settings.SetBool(KEY_MOVE, _robot_move);
    settings.SetInt(KEY_Y_HOME, _y_home_deg);
}

void AppMusicPlayer::save_last_track()
{
    if (_tracks.empty() || _index < 0 || _index >= static_cast<int>(_tracks.size())) {
        return;
    }
    Settings settings(NVS_NS, true);
    settings.SetInt(KEY_LAST_IDX, _index);
    std::string cur_path(_tracks[_index].path.c_str(), _tracks[_index].path.size());
    settings.SetString(KEY_LAST_PATH, cur_path);
}

bool AppMusicPlayer::save_head_cfg()
{
    save_settings();
    return true;
}

void AppMusicPlayer::move_home(int speed)
{
    auto& motion = GetStackChan().motion();
    motion.moveWithSpeed(py_x_deg_to_yaw(X_HOME_DEG), py_y_deg_to_pitch(_y_home_deg), speed);
}

void AppMusicPlayer::rgb_off()
{
    GetHAL().showRgbColor(0, 0, 0);
}

void AppMusicPlayer::set_all_rgb(uint32_t hex_color)
{
    uint8_t r = (hex_color >> 16) & 0xFF;
    uint8_t g = (hex_color >> 8) & 0xFF;
    uint8_t b = hex_color & 0xFF;
    GetHAL().showRgbColor(r, g, b);
}

void AppMusicPlayer::beat_update(uint32_t now)
{
    if (!_playing.load()) {
        return;
    }

    uint32_t beat = now / BEAT_MS;
    if (beat == _last_beat) {
        return;
    }
    _last_beat = beat;

    if (_lights_on) {
        set_all_rgb(PALETTE[beat % 8]);
    }

    if (_robot_move && (beat % 2) == 0) {
        int r = std::rand() % 100;
        if (r < SWAY_STILL_PCT) {
            return;
        }
        _sway_side = -_sway_side;
        int ax     = 3 + (std::rand() % (X_SWAY_DEG - 3 + 1));
        int ay     = 2 + (std::rand() % (Y_SWAY_DEG - 2 + 1));
        int my_deg = std::clamp(_y_home_deg + _sway_side * ay, 0, 90);
        int mx_deg = X_HOME_DEG + _sway_side * ax;

        auto& motion = GetStackChan().motion();
        motion.moveWithSpeed(py_x_deg_to_yaw(mx_deg), py_y_deg_to_pitch(my_deg), 550);
    }
}

void AppMusicPlayer::set_status(const std::string& msg, uint32_t duration_ms)
{
    _status_msg   = msg;
    _status_until = GetHAL().millis() + duration_ms;
}

std::string AppMusicPlayer::audio_state_summary() const
{
    auto& board      = Board::GetInstance();
    auto audio_codec = board.GetAudioCodec();
    bool spk         = audio_codec && audio_codec->output_enabled();
    bool mic         = audio_codec && audio_codec->input_enabled();
    uint32_t kb      = _decoded_bytes.load() / 1024;
    int batt         = GetHAL().getBatteryLevel();

    char buf[48];
    std::snprintf(buf, sizeof(buf), "S%d M%d V%d P%uK B%d", spk ? 1 : 0, mic ? 1 : 0, _volume,
                  static_cast<unsigned>(kb), batt);
    return std::string(buf);
}

/* -------------------------------------------------------------------------- */
/*                             LVGL UI Builder                                */
/* -------------------------------------------------------------------------- */

void AppMusicPlayer::build_ui()
{
    _root_screen = lv_screen_active();
    lv_obj_set_style_bg_color(_root_screen, lv_color_hex(COL_BG), 0);
    lv_obj_remove_flag(_root_screen, LV_OBJ_FLAG_SCROLLABLE);

    /* ============================ 1. MAIN VIEW ============================= */
    _main_cont = lv_obj_create(_root_screen);
    lv_obj_set_size(_main_cont, W, H);
    lv_obj_set_pos(_main_cont, 0, 0);
    style_flat_box(_main_cont, COL_BG, 0);

    // Track title top-left (tap to open playlist)
    _lbl_title = lv_label_create(_main_cont);
    lv_obj_set_pos(_lbl_title, 10, TITLE_Y);
    lv_obj_set_size(_lbl_title, 232, 20);
    lv_label_set_long_mode(_lbl_title, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_font(_lbl_title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(_lbl_title, lv_color_hex(COL_TEXT), 0);
    lv_obj_add_flag(_lbl_title, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(
        _lbl_title,
        [](lv_event_t* e) {
            auto* self = static_cast<AppMusicPlayer*>(lv_event_get_user_data(e));
            self->open_list();
        },
        LV_EVENT_CLICKED, this);

    // Battery widget top-right (tap for audio/battery diagnostic)
    _batt_cont = lv_obj_create(_main_cont);
    lv_obj_set_pos(_batt_cont, 244, 2);
    lv_obj_set_size(_batt_cont, 72, 20);
    style_flat_box(_batt_cont, COL_BG, 0);
    lv_obj_add_flag(_batt_cont, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(
        _batt_cont,
        [](lv_event_t* e) {
            auto* self = static_cast<AppMusicPlayer*>(lv_event_get_user_data(e));
            self->set_status(self->audio_state_summary());
            self->refresh_main_ui(GetHAL().millis());
        },
        LV_EVENT_CLICKED, this);

    _lbl_batt = lv_label_create(_batt_cont);
    lv_obj_set_pos(_lbl_batt, 0, 2);
    lv_obj_set_style_text_font(_lbl_batt, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(_lbl_batt, lv_color_hex(COL_DIM), 0);
    lv_label_set_text(_lbl_batt, "--%");

    _batt_box = lv_obj_create(_batt_cont);
    lv_obj_set_pos(_batt_box, 42, 4);
    lv_obj_set_size(_batt_box, 22, 12);
    style_flat_box(_batt_box, COL_BG, 2);
    lv_obj_set_style_border_width(_batt_box, 1, 0);
    lv_obj_set_style_border_color(_batt_box, lv_color_hex(COL_DIM), 0);
    lv_obj_remove_flag(_batt_box, LV_OBJ_FLAG_CLICKABLE);

    _batt_fill = lv_obj_create(_batt_box);
    lv_obj_set_pos(_batt_fill, 1, 1);
    lv_obj_set_size(_batt_fill, 16, 8);
    style_flat_box(_batt_fill, COL_GREEN, 1);
    lv_obj_remove_flag(_batt_fill, LV_OBJ_FLAG_CLICKABLE);

    _batt_tip = lv_obj_create(_batt_cont);
    lv_obj_set_pos(_batt_tip, 64, 7);
    lv_obj_set_size(_batt_tip, 3, 6);
    style_flat_box(_batt_tip, COL_DIM, 1);
    lv_obj_remove_flag(_batt_tip, LV_OBJ_FLAG_CLICKABLE);

    // Touch-seekable progress bar
    _bar_bg = lv_obj_create(_main_cont);
    lv_obj_set_pos(_bar_bg, BAR_X, BAR_Y);
    lv_obj_set_size(_bar_bg, BAR_W, BAR_H);
    style_flat_box(_bar_bg, COL_CARD_HI, 3);
    lv_obj_add_flag(_bar_bg, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(_bar_bg, 6);
    lv_obj_add_event_cb(
        _bar_bg,
        [](lv_event_t* e) {
            auto* self     = static_cast<AppMusicPlayer*>(lv_event_get_user_data(e));
            int32_t dur_ms = self->_duration_ms.load();
            if (dur_ms <= 0) {
                return;
            }
            lv_point_t p;
            lv_indev_get_point(lv_indev_active(), &p);
            float ratio = std::clamp(static_cast<float>(p.x - BAR_X) / static_cast<float>(BAR_W), 0.0f, 1.0f);
            self->seek_to(static_cast<int32_t>(ratio * dur_ms));
            self->refresh_main_ui(GetHAL().millis());
        },
        LV_EVENT_CLICKED, this);

    _bar_fill = lv_obj_create(_bar_bg);
    lv_obj_set_pos(_bar_fill, 0, 0);
    lv_obj_set_size(_bar_fill, 0, BAR_H);
    style_flat_box(_bar_fill, COL_ACCENT, 3);
    lv_obj_remove_flag(_bar_fill, LV_OBJ_FLAG_CLICKABLE);

    // Elapsed time, track counter, and total duration
    _lbl_elapsed = lv_label_create(_main_cont);
    lv_obj_set_pos(_lbl_elapsed, 10, TIME_Y);
    lv_obj_set_style_text_font(_lbl_elapsed, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(_lbl_elapsed, lv_color_hex(COL_DIM), 0);

    _lbl_track_num = lv_label_create(_main_cont);
    lv_obj_set_pos(_lbl_track_num, 180, TIME_Y);
    lv_obj_set_width(_lbl_track_num, 85);
    lv_obj_set_style_text_align(_lbl_track_num, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_font(_lbl_track_num, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(_lbl_track_num, lv_color_hex(COL_DIM), 0);

    _lbl_duration = lv_label_create(_main_cont);
    lv_obj_set_pos(_lbl_duration, 268, TIME_Y);
    lv_obj_set_width(_lbl_duration, 44);
    lv_obj_set_style_text_align(_lbl_duration, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_font(_lbl_duration, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(_lbl_duration, lv_color_hex(COL_DIM), 0);

    // Volume / version and playback status
    _lbl_vol_ver = lv_label_create(_main_cont);
    lv_obj_set_pos(_lbl_vol_ver, 10, STATUS_Y);
    lv_obj_set_style_text_font(_lbl_vol_ver, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(_lbl_vol_ver, lv_color_hex(COL_DIM), 0);

    _lbl_status = lv_label_create(_main_cont);
    lv_obj_set_pos(_lbl_status, 110, STATUS_Y);
    lv_obj_set_width(_lbl_status, 202);
    lv_obj_set_style_text_align(_lbl_status, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_long_mode(_lbl_status, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_font(_lbl_status, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(_lbl_status, lv_color_hex(COL_DIM), 0);

    // Center stage: animated classic StackChan face + floating musical notes
    _stage_cont = lv_obj_create(_main_cont);
    lv_obj_set_pos(_stage_cont, STAGE_X, STAGE_Y);
    lv_obj_set_size(_stage_cont, STAGE_W, STAGE_H);
    style_flat_box(_stage_cont, COL_CARD, 14);
    lv_obj_set_style_border_width(_stage_cont, 1, 0);
    lv_obj_set_style_border_color(_stage_cont, lv_color_hex(COL_CARD_HI), 0);

    _face_card = lv_obj_create(_stage_cont);
    lv_obj_set_size(_face_card, 118, 86);
    lv_obj_align(_face_card, LV_ALIGN_CENTER, 0, 0);
    style_flat_box(_face_card, COL_BG, 14);
    lv_obj_set_style_border_width(_face_card, 2, 0);
    lv_obj_set_style_border_color(_face_card, lv_color_hex(COL_CARD_HI), 0);
    lv_obj_add_flag(_face_card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(
        _face_card,
        [](lv_event_t* e) {
            auto* self = static_cast<AppMusicPlayer*>(lv_event_get_user_data(e));
            self->toggle_play();
            uint32_t now = GetHAL().millis();
            self->refresh_main_ui(now);
            self->refresh_avatar_anim(now);
        },
        LV_EVENT_CLICKED, this);

    _face_eye_l = lv_obj_create(_face_card);
    lv_obj_set_size(_face_eye_l, 18, 18);
    lv_obj_align(_face_eye_l, LV_ALIGN_CENTER, -26, -8);
    style_flat_box(_face_eye_l, COL_WHITE, LV_RADIUS_CIRCLE);
    lv_obj_remove_flag(_face_eye_l, LV_OBJ_FLAG_CLICKABLE);

    _face_eye_r = lv_obj_create(_face_card);
    lv_obj_set_size(_face_eye_r, 18, 18);
    lv_obj_align(_face_eye_r, LV_ALIGN_CENTER, 26, -8);
    style_flat_box(_face_eye_r, COL_WHITE, LV_RADIUS_CIRCLE);
    lv_obj_remove_flag(_face_eye_r, LV_OBJ_FLAG_CLICKABLE);

    _face_mouth = lv_obj_create(_face_card);
    lv_obj_set_size(_face_mouth, 38, 6);
    lv_obj_align(_face_mouth, LV_ALIGN_CENTER, 0, 18);
    style_flat_box(_face_mouth, COL_WHITE, 4);
    lv_obj_remove_flag(_face_mouth, LV_OBJ_FLAG_CLICKABLE);

    // 4 floating musical notes flanking the face
    const int note_xs[4] = {20, 54, 232, 266};
    for (int i = 0; i < 4; ++i) {
        lv_obj_t* n   = lv_label_create(_stage_cont);
        _note_lbls[i] = n;
        lv_label_set_text(n, LV_SYMBOL_AUDIO);
        lv_obj_set_style_text_font(n, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(n, lv_color_hex(PALETTE[i * 2]), 0);
        lv_obj_set_pos(n, note_xs[i], 52);
    }

    // Bottom 6-button bar: VOL- | << | > | >> | VOL+ | Settings
    for (int i = 0; i < 6; ++i) {
        const char* nm     = MAIN_BTN_NAMES[i];
        _main_btns[i].name = nm;

        lv_obj_t* btn     = lv_button_create(_main_cont);
        _main_btns[i].btn = btn;
        lv_obj_set_pos(btn, BAR6_X0 + i * (BAR6_W + BAR6_GAP), BAR6_Y);
        lv_obj_set_size(btn, BAR6_W, BAR6_H);
        style_flat_box(btn, COL_CARD, 10);
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_set_style_border_color(btn, lv_color_hex(COL_CARD_HI), 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(COL_ACCENT), LV_STATE_PRESSED);

        lv_obj_t* lbl       = lv_label_create(btn);
        _main_btns[i].label = lbl;
        lv_obj_center(lbl);
        if (std::strcmp(nm, "voldn") == 0 || std::strcmp(nm, "volup") == 0) {
            lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
        } else {
            lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
        }

        lv_obj_add_event_cb(
            btn,
            [](lv_event_t* e) {
                auto* self      = static_cast<AppMusicPlayer*>(lv_event_get_user_data(e));
                lv_obj_t* t_btn = static_cast<lv_obj_t*>(lv_event_get_target(e));
                const char* nm  = nullptr;
                for (const auto& mb : self->_main_btns) {
                    if (mb.btn == t_btn) {
                        nm = mb.name;
                        break;
                    }
                }
                if (!nm) {
                    return;
                }
                if (std::strcmp(nm, "voldn") == 0) {
                    self->change_volume(-VOLUME_STEP);
                } else if (std::strcmp(nm, "prev") == 0) {
                    self->change_track(-1);
                } else if (std::strcmp(nm, "play") == 0) {
                    self->toggle_play();
                } else if (std::strcmp(nm, "next") == 0) {
                    self->change_track(1);
                } else if (std::strcmp(nm, "volup") == 0) {
                    self->change_volume(VOLUME_STEP);
                } else if (std::strcmp(nm, "settings") == 0) {
                    self->switch_mode(ScreenMode::Settings);
                    return;
                }
                uint32_t now = GetHAL().millis();
                self->refresh_main_ui(now);
                self->refresh_avatar_anim(now);
            },
            LV_EVENT_CLICKED, this);
    }

    /* =========================== 2. SETTINGS VIEW ========================== */
    _settings_cont = lv_obj_create(_root_screen);
    lv_obj_set_size(_settings_cont, W, H);
    lv_obj_set_pos(_settings_cont, 0, 0);
    style_flat_box(_settings_cont, COL_BG, 0);
    lv_obj_add_flag(_settings_cont, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t* lbl_cfg_hdr = lv_label_create(_settings_cont);
    lv_obj_set_pos(lbl_cfg_hdr, 10, 10);
    lv_obj_set_size(lbl_cfg_hdr, 220, 24);
    lv_obj_set_style_text_font(lbl_cfg_hdr, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_cfg_hdr, lv_color_hex(COL_TEXT), 0);
    lv_label_set_text(lbl_cfg_hdr, "< SETTINGS");
    lv_obj_add_flag(lbl_cfg_hdr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(
        lbl_cfg_hdr,
        [](lv_event_t* e) {
            auto* self = static_cast<AppMusicPlayer*>(lv_event_get_user_data(e));
            self->switch_mode(ScreenMode::Main);
        },
        LV_EVENT_CLICKED, this);

    for (int i = 0; i < 7; ++i) {
        const auto& spec  = CFG_BTN_SPECS[i];
        _cfg_btns[i].name = spec.name;

        lv_obj_t* btn    = lv_button_create(_settings_cont);
        _cfg_btns[i].btn = btn;
        lv_obj_set_pos(btn, spec.x, spec.y);
        lv_obj_set_size(btn, spec.w, spec.h);
        style_flat_box(btn, COL_CARD, 10);
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_set_style_border_color(btn, lv_color_hex(COL_CARD_HI), 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(COL_ACCENT), LV_STATE_PRESSED);

        lv_obj_t* lbl      = lv_label_create(btn);
        _cfg_btns[i].label = lbl;
        lv_obj_center(lbl);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);

        lv_obj_add_event_cb(
            btn,
            [](lv_event_t* e) {
                auto* self      = static_cast<AppMusicPlayer*>(lv_event_get_user_data(e));
                lv_obj_t* t_btn = static_cast<lv_obj_t*>(lv_event_get_target(e));
                const char* nm  = nullptr;
                for (const auto& cb : self->_cfg_btns) {
                    if (cb.btn == t_btn) {
                        nm = cb.name;
                        break;
                    }
                }
                if (!nm) {
                    return;
                }
                if (std::strcmp(nm, "loop") == 0) {
                    self->cycle_loop();
                } else if (std::strcmp(nm, "shuf") == 0) {
                    self->toggle_shuffle();
                } else if (std::strcmp(nm, "lights") == 0) {
                    self->toggle_lights();
                } else if (std::strcmp(nm, "move") == 0) {
                    self->toggle_move();
                } else if (std::strcmp(nm, "playlist") == 0) {
                    self->open_list();
                    return;
                } else if (std::strcmp(nm, "head") == 0) {
                    self->switch_mode(ScreenMode::Head);
                    return;
                } else if (std::strcmp(nm, "menu") == 0) {
                    self->close();
                    return;
                }
                self->refresh_settings_ui();
            },
            LV_EVENT_CLICKED, this);
    }

    // Back button at the bottom of Settings
    lv_obj_t* btn_cfg_back = lv_button_create(_settings_cont);
    lv_obj_set_pos(btn_cfg_back, 60, 186);
    lv_obj_set_size(btn_cfg_back, 200, 38);
    style_flat_box(btn_cfg_back, COL_PLAY_ON, 10);
    lv_obj_t* lbl_back = lv_label_create(btn_cfg_back);
    lv_obj_center(lbl_back);
    lv_obj_set_style_text_font(lbl_back, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_back, lv_color_hex(COL_WHITE), 0);
    lv_label_set_text(lbl_back, "BACK");
    lv_obj_add_event_cb(
        btn_cfg_back,
        [](lv_event_t* e) {
            auto* self = static_cast<AppMusicPlayer*>(lv_event_get_user_data(e));
            self->switch_mode(ScreenMode::Main);
        },
        LV_EVENT_CLICKED, this);

    /* =========================== 3. PLAYLIST VIEW ========================== */
    _list_cont = lv_obj_create(_root_screen);
    lv_obj_set_size(_list_cont, W, H);
    lv_obj_set_pos(_list_cont, 0, 0);
    style_flat_box(_list_cont, COL_BG, 0);
    lv_obj_add_flag(_list_cont, LV_OBJ_FLAG_HIDDEN);

    _lbl_list_hdr = lv_label_create(_list_cont);
    lv_obj_set_pos(_lbl_list_hdr, 10, 4);
    lv_obj_set_size(_lbl_list_hdr, 130, 22);
    lv_obj_set_style_text_font(_lbl_list_hdr, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(_lbl_list_hdr, lv_color_hex(COL_TEXT), 0);
    lv_label_set_text(_lbl_list_hdr, "< PLAYLIST");
    lv_obj_add_flag(_lbl_list_hdr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(
        _lbl_list_hdr,
        [](lv_event_t* e) {
            auto* self = static_cast<AppMusicPlayer*>(lv_event_get_user_data(e));
            self->switch_mode(ScreenMode::Main);
        },
        LV_EVENT_CLICKED, this);

    _lbl_list_count = lv_label_create(_list_cont);
    lv_obj_set_pos(_lbl_list_count, 135, 7);
    lv_obj_set_width(_lbl_list_count, 177);
    lv_obj_set_style_text_align(_lbl_list_count, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_font(_lbl_list_count, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(_lbl_list_count, lv_color_hex(COL_DIM), 0);

    for (int i = 0; i < PAGE_ROWS; ++i) {
        lv_obj_t* row = lv_button_create(_list_cont);
        _list_rows[i] = row;
        lv_obj_set_pos(row, 8, 28 + i * 29);
        lv_obj_set_size(row, 304, 26);
        style_flat_box(row, COL_CARD, 8);

        lv_obj_t* rlbl    = lv_label_create(row);
        _list_row_lbls[i] = rlbl;
        lv_obj_set_pos(rlbl, 10, 4);
        lv_obj_set_width(rlbl, 284);
        lv_label_set_long_mode(rlbl, LV_LABEL_LONG_CLIP);
        lv_obj_set_style_text_font(rlbl, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(rlbl, lv_color_hex(COL_TEXT), 0);

        lv_obj_add_event_cb(
            row,
            [](lv_event_t* e) {
                auto* self      = static_cast<AppMusicPlayer*>(lv_event_get_user_data(e));
                lv_obj_t* t_row = static_cast<lv_obj_t*>(lv_event_get_target(e));
                for (int r = 0; r < PAGE_ROWS; ++r) {
                    if (self->_list_rows[r] == t_row) {
                        int track_idx = self->_list_page * PAGE_ROWS + r;
                        if (track_idx < static_cast<int>(self->_tracks.size())) {
                            self->_list_page = track_idx / PAGE_ROWS;
                            self->play_track(track_idx, 0);
                            self->switch_mode(ScreenMode::Main);
                        }
                        break;
                    }
                }
            },
            LV_EVENT_CLICKED, this);
    }

    _lbl_list_empty1 = lv_label_create(_list_cont);
    lv_obj_set_pos(_lbl_list_empty1, 22, 60);
    lv_obj_set_style_text_font(_lbl_list_empty1, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(_lbl_list_empty1, lv_color_hex(COL_DIM), 0);

    _lbl_list_empty2 = lv_label_create(_list_cont);
    lv_obj_set_pos(_lbl_list_empty2, 22, 84);
    lv_obj_set_style_text_font(_lbl_list_empty2, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(_lbl_list_empty2, lv_color_hex(COL_DIM), 0);

    _lbl_list_info = lv_label_create(_list_cont);
    lv_obj_set_pos(_lbl_list_info, 8, 178);
    lv_obj_set_width(_lbl_list_info, 304);
    lv_label_set_long_mode(_lbl_list_info, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_font(_lbl_list_info, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(_lbl_list_info, lv_color_hex(COL_DIM), 0);

    auto make_list_footer_btn = [&](int x, int w, const char* txt, uint32_t bg = COL_CARD_HI) {
        lv_obj_t* b = lv_button_create(_list_cont);
        lv_obj_set_pos(b, x, 204);
        lv_obj_set_size(b, w, 30);
        style_flat_box(b, bg, 8);
        lv_obj_t* l = lv_label_create(b);
        lv_obj_center(l);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(l, lv_color_hex(COL_TEXT), 0);
        lv_label_set_text(l, txt);
        return b;
    };

    _btn_page_prev = make_list_footer_btn(8, 72, "PREV");
    lv_obj_add_event_cb(
        _btn_page_prev,
        [](lv_event_t* e) {
            auto* self = static_cast<AppMusicPlayer*>(lv_event_get_user_data(e));
            if (self->_list_page > 0) {
                self->_list_page--;
                self->refresh_list_ui();
            }
        },
        LV_EVENT_CLICKED, this);

    _btn_page_next = make_list_footer_btn(85, 72, "NEXT");
    lv_obj_add_event_cb(
        _btn_page_next,
        [](lv_event_t* e) {
            auto* self = static_cast<AppMusicPlayer*>(lv_event_get_user_data(e));
            int pages  = std::max(1, (static_cast<int>(self->_tracks.size()) + PAGE_ROWS - 1) / PAGE_ROWS);
            if (self->_list_page + 1 < pages) {
                self->_list_page++;
                self->refresh_list_ui();
            }
        },
        LV_EVENT_CLICKED, this);

    _btn_open_head = make_list_footer_btn(162, 76, "SETUP");
    lv_obj_add_event_cb(
        _btn_open_head,
        [](lv_event_t* e) {
            auto* self = static_cast<AppMusicPlayer*>(lv_event_get_user_data(e));
            self->switch_mode(ScreenMode::Settings);
        },
        LV_EVENT_CLICKED, this);

    _btn_exit_app = make_list_footer_btn(243, 69, "MENU", COL_ACCENT_DK);
    lv_obj_add_event_cb(
        _btn_exit_app,
        [](lv_event_t* e) {
            auto* self = static_cast<AppMusicPlayer*>(lv_event_get_user_data(e));
            self->close();
        },
        LV_EVENT_CLICKED, this);

    /* =========================== 4. HEAD VIEW ============================== */
    _head_cont = lv_obj_create(_root_screen);
    lv_obj_set_size(_head_cont, W, H);
    lv_obj_set_pos(_head_cont, 0, 0);
    style_flat_box(_head_cont, COL_BG, 0);
    lv_obj_add_flag(_head_cont, LV_OBJ_FLAG_HIDDEN);

    _lbl_head_hdr = lv_label_create(_head_cont);
    lv_obj_set_pos(_lbl_head_hdr, 10, 6);
    lv_obj_set_size(_lbl_head_hdr, 240, 22);
    lv_obj_set_style_text_font(_lbl_head_hdr, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(_lbl_head_hdr, lv_color_hex(COL_TEXT), 0);
    lv_label_set_text(_lbl_head_hdr, "< HEAD CALIBRATION");
    lv_obj_add_flag(_lbl_head_hdr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(
        _lbl_head_hdr,
        [](lv_event_t* e) {
            auto* self = static_cast<AppMusicPlayer*>(lv_event_get_user_data(e));
            self->switch_mode(ScreenMode::Settings);
        },
        LV_EVENT_CLICKED, this);

    lv_obj_t* head_card = lv_obj_create(_head_cont);
    lv_obj_set_pos(head_card, 20, 40);
    lv_obj_set_size(head_card, 280, 150);
    style_flat_box(head_card, COL_CARD, 12);

    lv_obj_t* lbl_obj_cap = lv_label_create(_head_cont);
    lv_obj_set_pos(lbl_obj_cap, 56, 56);
    lv_obj_set_style_text_font(lbl_obj_cap, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_obj_cap, lv_color_hex(COL_DIM), 0);
    lv_label_set_text(lbl_obj_cap, "TARGET");

    lv_obj_t* lbl_real_cap = lv_label_create(_head_cont);
    lv_obj_set_pos(lbl_real_cap, 206, 56);
    lv_obj_set_style_text_font(lbl_real_cap, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_real_cap, lv_color_hex(COL_DIM), 0);
    lv_label_set_text(lbl_real_cap, "ACTUAL");

    _lbl_head_target = lv_label_create(_head_cont);
    lv_obj_set_pos(_lbl_head_target, 64, 78);
    lv_obj_set_style_text_font(_lbl_head_target, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(_lbl_head_target, lv_color_hex(COL_YELLOW), 0);

    _lbl_head_real = lv_label_create(_head_cont);
    lv_obj_set_pos(_lbl_head_real, 206, 78);
    lv_obj_set_style_text_font(_lbl_head_real, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(_lbl_head_real, lv_color_hex(COL_TEXT), 0);

    _btn_head_minus = lv_button_create(_head_cont);
    lv_obj_set_pos(_btn_head_minus, 36, 126);
    lv_obj_set_size(_btn_head_minus, 84, 54);
    style_flat_box(_btn_head_minus, COL_CARD_HI, 10);
    lv_obj_t* lbl_minus = lv_label_create(_btn_head_minus);
    lv_obj_center(lbl_minus);
    lv_obj_set_style_text_font(lbl_minus, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(lbl_minus, lv_color_hex(COL_TEXT), 0);
    lv_label_set_text(lbl_minus, "-");
    lv_obj_add_event_cb(
        _btn_head_minus,
        [](lv_event_t* e) {
            auto* self        = static_cast<AppMusicPlayer*>(lv_event_get_user_data(e));
            self->_y_home_deg = std::max(0, self->_y_home_deg - HEAD_STEP_DEG);
            self->move_home(500);
            self->refresh_head_ui();
        },
        LV_EVENT_CLICKED, this);

    _btn_head_plus = lv_button_create(_head_cont);
    lv_obj_set_pos(_btn_head_plus, 200, 126);
    lv_obj_set_size(_btn_head_plus, 84, 54);
    style_flat_box(_btn_head_plus, COL_CARD_HI, 10);
    lv_obj_t* lbl_plus = lv_label_create(_btn_head_plus);
    lv_obj_center(lbl_plus);
    lv_obj_set_style_text_font(lbl_plus, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(lbl_plus, lv_color_hex(COL_TEXT), 0);
    lv_label_set_text(lbl_plus, "+");
    lv_obj_add_event_cb(
        _btn_head_plus,
        [](lv_event_t* e) {
            auto* self        = static_cast<AppMusicPlayer*>(lv_event_get_user_data(e));
            self->_y_home_deg = std::min(90, self->_y_home_deg + HEAD_STEP_DEG);
            self->move_home(500);
            self->refresh_head_ui();
        },
        LV_EVENT_CLICKED, this);

    _btn_head_save = lv_button_create(_head_cont);
    lv_obj_set_pos(_btn_head_save, 70, 196);
    lv_obj_set_size(_btn_head_save, 180, 34);
    style_flat_box(_btn_head_save, COL_PLAY_ON, 10);
    lv_obj_t* lbl_save = lv_label_create(_btn_head_save);
    lv_obj_center(lbl_save);
    lv_obj_set_style_text_font(lbl_save, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_save, lv_color_hex(COL_WHITE), 0);
    lv_label_set_text(lbl_save, "SAVE");
    lv_obj_add_event_cb(
        _btn_head_save,
        [](lv_event_t* e) {
            auto* self = static_cast<AppMusicPlayer*>(lv_event_get_user_data(e));
            self->save_head_cfg();
            self->set_status("HEAD SAVED");
            self->switch_mode(ScreenMode::Main);
        },
        LV_EVENT_CLICKED, this);

    refresh_battery_ui();
}

void AppMusicPlayer::destroy_ui()
{
    if (_main_cont) {
        lv_obj_delete(_main_cont);
        _main_cont = nullptr;
    }
    if (_settings_cont) {
        lv_obj_delete(_settings_cont);
        _settings_cont = nullptr;
    }
    if (_list_cont) {
        lv_obj_delete(_list_cont);
        _list_cont = nullptr;
    }
    if (_head_cont) {
        lv_obj_delete(_head_cont);
        _head_cont = nullptr;
    }
}

void AppMusicPlayer::switch_mode(ScreenMode new_mode)
{
    _mode = new_mode;
    if (!_main_cont || !_settings_cont || !_list_cont || !_head_cont) {
        return;
    }

    lv_obj_add_flag(_main_cont, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(_settings_cont, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(_list_cont, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(_head_cont, LV_OBJ_FLAG_HIDDEN);

    if (_mode == ScreenMode::Main) {
        lv_obj_remove_flag(_main_cont, LV_OBJ_FLAG_HIDDEN);
        uint32_t now = GetHAL().millis();
        refresh_main_ui(now);
        refresh_avatar_anim(now);
    } else if (_mode == ScreenMode::Settings) {
        lv_obj_remove_flag(_settings_cont, LV_OBJ_FLAG_HIDDEN);
        refresh_settings_ui();
    } else if (_mode == ScreenMode::List) {
        lv_obj_remove_flag(_list_cont, LV_OBJ_FLAG_HIDDEN);
        refresh_list_ui();
    } else if (_mode == ScreenMode::Head) {
        lv_obj_remove_flag(_head_cont, LV_OBJ_FLAG_HIDDEN);
        refresh_head_ui();
    }
}

void AppMusicPlayer::open_list()
{
    if (_tracks.empty()) {
        if (!_sd_mounted) {
            _sd_mounted = mount_sd();
        }
        scan_tracks();
        if (!_tracks.empty()) {
            auto info    = probe_audio(std::string(_tracks[0].path.c_str()), _tracks[0].is_mp3);
            _duration_ms = info.duration_ms;
            _byte_rate   = info.byte_rate;
        }
    }
    if (!_tracks.empty()) {
        _list_page = _index / PAGE_ROWS;
    }
    switch_mode(ScreenMode::List);
}

/* -------------------------------------------------------------------------- */
/*                             UI View Updates                                */
/* -------------------------------------------------------------------------- */

void AppMusicPlayer::refresh_battery_ui()
{
    if (!_lbl_batt || !_batt_fill) {
        return;
    }
    _batt_pct = GetHAL().getBatteryLevel();
    _batt_chg = GetHAL().isBatteryCharging();

    char buf[16];
    std::snprintf(buf, sizeof(buf), "%d%%", _batt_pct);
    lv_label_set_text(_lbl_batt, buf);

    int inner = 18;
    int fw    = std::clamp((_batt_pct * inner) / 100, 1, inner);
    lv_obj_set_width(_batt_fill, fw);

    uint32_t col = COL_RED;
    if (_batt_pct > 50) {
        col = COL_GREEN;
    } else if (_batt_pct > 20) {
        col = COL_YELLOW;
    }
    lv_obj_set_style_bg_color(_batt_fill, lv_color_hex(col), 0);
}

void AppMusicPlayer::refresh_button_styles()
{
    bool is_playing = _playing.load();
    for (int i = 0; i < 6; ++i) {
        const char* nm = _main_btns[i].name;
        lv_obj_t* btn  = _main_btns[i].btn;
        lv_obj_t* lbl  = _main_btns[i].label;
        if (!btn || !lbl) {
            continue;
        }

        uint32_t bg_col  = COL_CARD;
        uint32_t txt_col = COL_TEXT;
        const char* text = "";

        if (std::strcmp(nm, "voldn") == 0) {
            text = "VOL-";
        } else if (std::strcmp(nm, "prev") == 0) {
            text = LV_SYMBOL_PREV;
        } else if (std::strcmp(nm, "play") == 0) {
            bg_col  = is_playing ? COL_PLAY_ON : COL_CARD;
            txt_col = is_playing ? COL_WHITE : COL_TEXT;
            text    = is_playing ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY;
        } else if (std::strcmp(nm, "next") == 0) {
            text = LV_SYMBOL_NEXT;
        } else if (std::strcmp(nm, "volup") == 0) {
            text = "VOL+";
        } else if (std::strcmp(nm, "settings") == 0) {
            text = LV_SYMBOL_SETTINGS;
        }

        lv_obj_set_style_bg_color(btn, lv_color_hex(bg_col), 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(txt_col), 0);
        lv_label_set_text(lbl, text);
    }
}

void AppMusicPlayer::refresh_settings_ui()
{
    if (!_settings_cont) {
        return;
    }

    for (int i = 0; i < 7; ++i) {
        const char* nm = _cfg_btns[i].name;
        lv_obj_t* btn  = _cfg_btns[i].btn;
        lv_obj_t* lbl  = _cfg_btns[i].label;
        if (!btn || !lbl) {
            continue;
        }

        uint32_t bg_col  = COL_CARD;
        uint32_t txt_col = COL_TEXT;
        std::string text;

        if (std::strcmp(nm, "loop") == 0) {
            bg_col  = (_loop_mode != 0) ? COL_ACCENT_DK : COL_CARD;
            txt_col = (_loop_mode != 0) ? COL_YELLOW : COL_DIM;
            text    = std::string("LOOP\n") + LOOP_NAMES[_loop_mode];
        } else if (std::strcmp(nm, "shuf") == 0) {
            bg_col  = _shuffle_on ? COL_ACCENT_DK : COL_CARD;
            txt_col = _shuffle_on ? COL_WHITE : COL_DIM;
            text    = std::string("SHUFFLE\n") + (_shuffle_on ? "ON" : "OFF");
        } else if (std::strcmp(nm, "lights") == 0) {
            bg_col  = _lights_on ? COL_ACCENT_DK : COL_CARD;
            txt_col = _lights_on ? COL_WHITE : COL_DIM;
            text    = std::string("LIGHTS\n") + (_lights_on ? "ON" : "OFF");
        } else if (std::strcmp(nm, "move") == 0) {
            bg_col  = _robot_move ? COL_ACCENT_DK : COL_CARD;
            txt_col = _robot_move ? COL_WHITE : COL_DIM;
            text    = std::string("MOVE\n") + (_robot_move ? "ON" : "OFF");
        } else if (std::strcmp(nm, "playlist") == 0) {
            bg_col  = COL_CARD_HI;
            txt_col = COL_WHITE;
            text    = "PLAYLIST";
        } else if (std::strcmp(nm, "head") == 0) {
            bg_col  = COL_CARD_HI;
            txt_col = COL_TEXT;
            text    = "HEAD";
        } else if (std::strcmp(nm, "menu") == 0) {
            bg_col  = COL_CARD_HI;
            txt_col = COL_YELLOW;
            text    = "EXIT";
        }

        lv_obj_set_style_bg_color(btn, lv_color_hex(bg_col), 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(txt_col), 0);
        lv_label_set_text(lbl, text.c_str());
    }
}

void AppMusicPlayer::refresh_main_ui(uint32_t now)
{
    (void)now;
    if (!_main_cont) {
        return;
    }

    if (!_tracks.empty()) {
        lv_obj_set_style_text_color(_lbl_title, lv_color_hex(COL_TEXT), 0);
        std::string cur_title(_tracks[_index].title.c_str(), _tracks[_index].title.size());
        lv_label_set_text(_lbl_title, clip_text(cur_title, 20).c_str());

        char tnum[32];
        std::snprintf(tnum, sizeof(tnum), "%d/%d", _index + 1, static_cast<int>(_tracks.size()));
        lv_label_set_text(_lbl_track_num, tnum);
    } else {
        lv_obj_set_style_text_color(_lbl_title, lv_color_hex(COL_RED), 0);
        lv_label_set_text(_lbl_title, "NO MUSIC");
        lv_label_set_text(_lbl_track_num, "");
    }

    int32_t el_ms  = _elapsed_ms.load();
    int32_t dur_ms = _duration_ms.load();
    lv_label_set_text(_lbl_elapsed, fmt_time(el_ms).c_str());
    lv_label_set_text(_lbl_duration, fmt_time(dur_ms).c_str());

    float ratio = 0.0f;
    if (dur_ms > 0) {
        ratio = std::clamp(static_cast<float>(el_ms) / static_cast<float>(dur_ms), 0.0f, 1.0f);
    }
    int fw = static_cast<int>(BAR_W * ratio);
    lv_obj_set_width(_bar_fill, std::max(0, fw));

    std::string vol_line = "VOL " + std::to_string(_volume) + " " + APP_VERSION;
    lv_label_set_text(_lbl_vol_ver, vol_line.c_str());

    std::string status_line = std::string(_playing.load() ? "PLAYING" : "PAUSED") + "  LOOP " + LOOP_NAMES[_loop_mode];
    uint32_t status_col     = COL_DIM;
    if (!_status_msg.empty()) {
        status_line = _status_msg;
        status_col  = COL_YELLOW;
    } else if (!_audio_err.empty()) {
        status_line = _audio_err;
        status_col  = COL_RED;
    }
    lv_obj_set_style_text_color(_lbl_status, lv_color_hex(status_col), 0);
    lv_label_set_text(_lbl_status, clip_text(status_line, 26).c_str());

    refresh_button_styles();
}

void AppMusicPlayer::refresh_avatar_anim(uint32_t now)
{
    if (!_face_card || !_face_eye_l || !_face_eye_r || !_face_mouth) {
        return;
    }

    bool is_playing = _playing.load();

    // Animate the classic StackChan face on the beat
    int offset_y = 0;
    int offset_x = 0;
    if (is_playing) {
        float beat_phase = static_cast<float>(now % BEAT_MS) / static_cast<float>(BEAT_MS);
        float pulse      = std::abs(std::sin(beat_phase * 3.14159f));
        offset_y         = static_cast<int>(-pulse * 4.0f) + 2;
        offset_x         = static_cast<int>(std::sin((now / static_cast<float>(BEAT_MS)) * 1.5708f) * 3.0f);

        int mouth_h = 6 + static_cast<int>(pulse * 12.0f);
        int mouth_w = 34 - static_cast<int>(pulse * 8.0f);
        lv_obj_set_size(_face_mouth, mouth_w, mouth_h);
        lv_obj_set_style_radius(_face_mouth, (mouth_h > 10) ? 8 : 4, 0);

        bool blink = ((now % 4200) < 160);
        lv_obj_set_size(_face_eye_l, 18, blink ? 4 : 18);
        lv_obj_set_size(_face_eye_r, 18, blink ? 4 : 18);
    } else {
        float breath = std::sin(static_cast<float>(now % 3000) / 3000.0f * 6.28318f);
        offset_y     = static_cast<int>(breath * 2.0f);
        lv_obj_set_size(_face_mouth, 38, 6);
        lv_obj_set_style_radius(_face_mouth, 4, 0);

        bool blink = ((now % 4500) < 150);
        lv_obj_set_size(_face_eye_l, 18, blink ? 4 : 18);
        lv_obj_set_size(_face_eye_r, 18, blink ? 4 : 18);
    }
    lv_obj_align(_face_card, LV_ALIGN_CENTER, offset_x, offset_y);

    // Floating musical notes on the sides
    const int base_xs[4] = {18, 54, 232, 268};
    for (int i = 0; i < 4; ++i) {
        if (!_note_lbls[i]) {
            continue;
        }
        if (is_playing) {
            lv_obj_remove_flag(_note_lbls[i], LV_OBJ_FLAG_HIDDEN);
            float phase = static_cast<float>((now + i * 690) % 2760) / 2760.0f;
            int ny      = 92 - static_cast<int>(phase * 72.0f);
            int nx      = base_xs[i] + static_cast<int>(std::sin(phase * 6.28318f + i) * 5.0f);
            lv_obj_set_pos(_note_lbls[i], nx, ny);
            lv_obj_set_style_text_color(_note_lbls[i], lv_color_hex(PALETTE[((now / BEAT_MS) + i) % 8]), 0);
        } else {
            lv_obj_add_flag(_note_lbls[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void AppMusicPlayer::refresh_list_ui()
{
    if (!_list_cont) {
        return;
    }

    int track_count = static_cast<int>(_tracks.size());
    int pages       = std::max(1, (track_count + PAGE_ROWS - 1) / PAGE_ROWS);
    _list_page      = std::clamp(_list_page, 0, pages - 1);

    char cbuf[48];
    std::snprintf(cbuf, sizeof(cbuf), "%d TRACKS  %d/%d", track_count, _list_page + 1, pages);
    lv_label_set_text(_lbl_list_count, cbuf);

    for (int i = 0; i < PAGE_ROWS; ++i) {
        int t = _list_page * PAGE_ROWS + i;
        if (t < track_count) {
            lv_obj_remove_flag(_list_rows[i], LV_OBJ_FLAG_HIDDEN);
            bool is_cur = (t == _index);
            lv_obj_set_style_bg_color(_list_rows[i], lv_color_hex(is_cur ? COL_ACCENT_DK : COL_CARD), 0);
            lv_obj_set_style_text_color(_list_row_lbls[i], lv_color_hex(is_cur ? COL_WHITE : COL_TEXT), 0);
            std::string row_title(_tracks[t].title.c_str(), _tracks[t].title.size());
            lv_label_set_text(_list_row_lbls[i], clip_text(row_title, 26).c_str());
        } else {
            lv_obj_add_flag(_list_rows[i], LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (track_count == 0) {
        lv_obj_remove_flag(_lbl_list_empty1, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(_lbl_list_empty2, LV_OBJ_FLAG_HIDDEN);
        if (!_sd_mounted) {
            lv_label_set_text(_lbl_list_empty1, "SD NOT MOUNTED");
            lv_label_set_text(_lbl_list_empty2, "FORMAT AS FAT32");
        } else if (_scan_error) {
            lv_label_set_text(_lbl_list_empty1, "SD READ ERROR");
            lv_label_set_text(_lbl_list_empty2, "TOO MANY FILES?");
        } else {
            lv_label_set_text(_lbl_list_empty1, "NO WAV/MP3 IN /SD");
            lv_label_set_text(_lbl_list_empty2, "CHECK ROOT FOLDER");
        }
    } else {
        lv_obj_add_flag(_lbl_list_empty1, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(_lbl_list_empty2, LV_OBJ_FLAG_HIDDEN);
    }

    uint32_t free_kb = static_cast<uint32_t>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) / 1024);
    char ibuf[96];
    std::snprintf(ibuf, sizeof(ibuf), "%d ITEMS  %d AUDIO  M:%uK", _scan_entries, track_count,
                  static_cast<unsigned>(free_kb));
    std::string info_str(ibuf);
    if (_scan_other_n > 0) {
        info_str += "  " + _scan_other_ext + ":" + std::to_string(_scan_other_n);
    }
    lv_label_set_text(_lbl_list_info, clip_text(info_str, 44).c_str());
}

void AppMusicPlayer::refresh_head_ui()
{
    if (!_head_cont) {
        return;
    }

    lv_label_set_text(_lbl_head_target, std::to_string(_y_home_deg).c_str());

    int real_pitch = GetStackChan().motion().getCurrentPitchAngle();
    int real_y_deg = pitch_to_py_y_deg(real_pitch);
    lv_label_set_text(_lbl_head_real, std::to_string(real_y_deg).c_str());
}
