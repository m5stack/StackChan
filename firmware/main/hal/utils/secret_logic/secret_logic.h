/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <string>
#include <string_view>

namespace secret_logic {

std::string get_server_url();
std::string generate_auth_token();
std::string generate_handshake_token(std::string_view data);

}  // namespace secret_logic
