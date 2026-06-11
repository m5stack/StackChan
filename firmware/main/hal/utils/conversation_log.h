/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <string_view>

namespace conversation_log {

void append_message(std::string_view role, std::string_view content);
void append_system(std::string_view content);

}  // namespace conversation_log
