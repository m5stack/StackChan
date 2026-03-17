/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "reminder_view.hpp"
#include <smooth_ui_toolkit.hpp>
#include <uitk/short_namespace.hpp>
#include <string_view>

namespace tools {

struct ReminderInfo_t {
    int id              = -1;
    uint32_t durationMs = 0;
    std::string message;
    bool repeat = false;
};

uitk::Signal<int, std::string_view>& on_reminder_triggered();
int create_reminder(uint32_t durationMs, std::string_view message, bool repeat = false);
void stop_reminder(int id);
void update_reminders();
std::vector<ReminderInfo_t> get_active_reminders();

}  // namespace tools
