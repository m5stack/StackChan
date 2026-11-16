/*
 * SPDX-License-Identifier: MIT
 *
 * LAN endpoint for the reefwatch camera detector (github.com/adampog/reefwatch):
 *
 *   POST /frame  JPEG body (<= 320x240), headers X-Reefwatch-Token, X-Camera, X-Kind (detection|live), X-Labels
 *   GET  /live   {"camera": "front" | null, "fps": 5}, polled by reefwatch to know what to stream live
 */
#include "hal.h"
#include "hal_lan_server.h"
#include "utils/jpeg_to_image/jpeg_decoder.h"
#include <mooncake_log.h>
#include <esp_heap_caps.h>
#include <cstring>
#include <mutex>

static const std::string_view _tag = "HAL-Reefwatch";

static std::mutex _live_mutex;
static std::string _live_camera;
static uint32_t _live_started_at = 0;

// Live view switches itself off so a forgotten stream doesn't run all night
static constexpr uint32_t _live_max_ms = 5 * 60 * 1000;

void Hal::setReefwatchLiveCamera(std::string_view camera)
{
    // Camera names end up in JSON and on screen, so keep them plain
    std::string name;
    for (char c : camera.substr(0, 32)) {
        if (isalnum((unsigned char)c) || c == '-' || c == '_') {
            name += c;
        }
    }

    std::lock_guard<std::mutex> lock(_live_mutex);
    if (name != _live_camera) {
        mclog::tagInfo(_tag, "live view: {}", name.empty() ? "off" : name);
    }
    _live_camera     = name;
    _live_started_at = millis();
}

std::string Hal::getReefwatchLiveCamera()
{
    std::lock_guard<std::mutex> lock(_live_mutex);
    if (!_live_camera.empty() && millis() - _live_started_at > _live_max_ms) {
        mclog::tagInfo(_tag, "live view timed out");
        _live_camera.clear();
    }
    return _live_camera;
}

#if CONFIG_STACKCHAN_REEFWATCH_ENABLE

static constexpr size_t _max_jpeg_size = 96 * 1024;

static std::string get_header(httpd_req_t* req, const char* field, size_t maxLen = 128)
{
    size_t len = httpd_req_get_hdr_value_len(req, field);
    if (len == 0) {
        return {};
    }
    std::string value(std::min(len, maxLen) + 1, '\0');
    httpd_req_get_hdr_value_str(req, field, value.data(), value.size());  // truncates past maxLen
    value.resize(strlen(value.c_str()));
    return value;
}

static bool check_token(httpd_req_t* req)
{
    const char* expected = CONFIG_STACKCHAN_REEFWATCH_TOKEN;
    if (expected[0] == '\0' || get_header(req, "X-Reefwatch-Token") == expected) {
        return true;
    }
    httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "bad token");
    return false;
}

static esp_err_t send_no_content(httpd_req_t* req)
{
    httpd_resp_set_status(req, "204 No Content");
    return httpd_resp_send(req, nullptr, 0);
}

static esp_err_t frame_handler(httpd_req_t* req)
{
    if (!check_token(req)) {
        return ESP_OK;
    }

    size_t len = req->content_len;
    if (len == 0 || len > _max_jpeg_size) {
        return httpd_resp_send_err(req, HTTPD_413_CONTENT_TOO_LARGE, "jpeg must be 1 byte to 96 KB");
    }

    std::unique_ptr<uint8_t, decltype(&heap_caps_free)> jpeg(
        (uint8_t*)heap_caps_malloc(len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT), heap_caps_free);
    if (!jpeg) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "out of memory");
    }

    size_t received = 0;
    while (received < len) {
        int ret = httpd_req_recv(req, (char*)jpeg.get() + received, len - received);
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (ret <= 0) {
            return ESP_FAIL;
        }
        received += ret;
    }

    ReefwatchFrame_t frame;
    frame.camera = get_header(req, "X-Camera", 32);
    frame.labels = get_header(req, "X-Labels");
    frame.isLive = get_header(req, "X-Kind", 16) == "live";

    // A live frame that was already in flight when the live view was switched off
    if (frame.isLive && frame.camera != GetHAL().getReefwatchLiveCamera()) {
        return send_no_content(req);
    }

    frame.image = jpeg_dec::decode_to_lvgl(jpeg.get(), len);
    if (!frame.image) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "could not decode jpeg");
    }

    GetHAL().onReefwatchFrame.emit(frame);
    return send_no_content(req);
}

static esp_err_t live_handler(httpd_req_t* req)
{
    if (!check_token(req)) {
        return ESP_OK;
    }

    auto camera = GetHAL().getReefwatchLiveCamera();
    auto body   = fmt::format(R"({{"camera": {}, "fps": {}}})",
                              camera.empty() ? "null" : fmt::format("\"{}\"", camera),
                              CONFIG_STACKCHAN_REEFWATCH_LIVE_FPS);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, body.data(), body.size());
}

void reefwatch_register_uris(httpd_handle_t server)
{
    httpd_uri_t frame_uri = {.uri = "/frame", .method = HTTP_POST, .handler = frame_handler, .user_ctx = nullptr};
    httpd_uri_t live_uri  = {.uri = "/live", .method = HTTP_GET, .handler = live_handler, .user_ctx = nullptr};
    httpd_register_uri_handler(server, &frame_uri);
    httpd_register_uri_handler(server, &live_uri);

    if (CONFIG_STACKCHAN_REEFWATCH_TOKEN[0] == 0) {
        mclog::tagWarn(_tag, "no token set, any device on the LAN can post frames");
    }
}

#endif
