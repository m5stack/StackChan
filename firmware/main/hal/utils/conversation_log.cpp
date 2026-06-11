/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "conversation_log.h"

#include <cstdio>
#include <ctime>
#include <string>

#include <esp_log.h>
#include <esp_timer.h>

static const char* TAG = "ConvLog";
static const char* kConversationLogPath = "/sdcard/stackchan_conversation.log";

namespace {

std::string make_timestamp()
{
    const time_t now = time(nullptr);
    struct tm tm_now {};
    localtime_r(&now, &tm_now);

    char buf[32];
    if (strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_now) == 0) {
        return "1970-01-01 00:00:00";
    }
    return buf;
}

void append_line(std::string_view line)
{
    FILE* fp = fopen(kConversationLogPath, "a");
    if (!fp) {
        return;
    }

    fwrite(line.data(), 1, line.size(), fp);
    fwrite("\n", 1, 1, fp);
    fclose(fp);
}

}  // namespace

namespace conversation_log {

void append_message(std::string_view role, std::string_view content)
{
    if (content.empty()) {
        return;
    }

    const std::string timestamp = make_timestamp();
    std::string line;
    line.reserve(timestamp.size() + role.size() + content.size() + 8);
    line.append("[");
    line.append(timestamp);
    line.append("] ");
    line.append(role);
    line.append(": ");
    line.append(content);

    append_line(line);
}

void append_system(std::string_view content)
{
    append_message("system", content);
}

}  // namespace conversation_log
