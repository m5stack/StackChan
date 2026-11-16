/*
 * SPDX-License-Identifier: MIT
 *
 * Services hosted on the robot's LAN HTTP server (hal_lan_server.cpp).
 */
#pragma once
#include <esp_http_server.h>
#include <sdkconfig.h>

class McpTool;

#if CONFIG_STACKCHAN_REEFWATCH_ENABLE
void reefwatch_register_uris(httpd_handle_t server);
#endif

// Tools added here are also served to LAN agents when CONFIG_STACKCHAN_LAN_MCP_ENABLE is set
void lan_mcp_add_tool(McpTool* tool);

#if CONFIG_STACKCHAN_LAN_MCP_ENABLE
void lan_mcp_register_uris(httpd_handle_t server);
#endif
