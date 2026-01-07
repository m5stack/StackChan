/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal.h"
#include <mooncake_log.h>
#include <mcp_server.h>
#include <stackchan/stackchan.h>

using namespace stackchan;

static const std::string _tag = "HAL-MCP";

void Hal::xiaozhi_mcp_init()
{
    mclog::tagInfo(_tag, "init");

    // https://github.com/78/xiaozhi-esp32/blob/main/docs/mcp-usage.md
    auto& mcp_server = McpServer::GetInstance();

    mclog::tagInfo(_tag, "add motion.set_angles tool");
    mcp_server.AddTool(
        "self.motion.set_angles",
        "Set the angles of the robot's servos (head movement). Yaw controls left/right (-1280 to "
        "1280), Pitch controls up/down (0 to 900). Note: 10 units equals 1 degree. For natural movement, prefer angles "
        "within +/- 300 (30 degrees) unless instructed otherwise. Speed controls the movement speed (100-1000).",
        PropertyList({Property("yaw", kPropertyTypeInteger, 0, -1280, 1280),
                      Property("pitch", kPropertyTypeInteger, 0, 0, 900),
                      Property("speed", kPropertyTypeInteger, 150, 100, 1000)}),
        [this](const PropertyList& properties) -> ReturnValue {
            int pitch = properties["pitch"].value<int>();
            int yaw   = properties["yaw"].value<int>();
            int speed = properties["speed"].value<int>();

            mclog::tagInfo(_tag, "motion set angles: pitch: {}, yaw: {}, speed: {}", pitch, yaw, speed);

            auto& motion = GetStackChan().motion();
            motion.pitchServo().moveWithSpeed(pitch, speed);
            motion.yawServo().moveWithSpeed(yaw, speed);

            return true;
        });

    mclog::tagInfo(_tag, "add led.set_color tool");
    mcp_server.AddTool("self.led.set_color",
                       "Set the color of the RGB LED strip. Each channel (red, green, blue) ranges from 0 to 255. "
                       "Note: For power saving and eye protection, it is recommended to keep each channel value below "
                       "126.",
                       PropertyList({Property("red", kPropertyTypeInteger, 0, 0, 168),
                                     Property("green", kPropertyTypeInteger, 0, 0, 168),
                                     Property("blue", kPropertyTypeInteger, 0, 0, 168)}),
                       [this](const PropertyList& properties) -> ReturnValue {
                           int r = properties["red"].value<int>();
                           int g = properties["green"].value<int>();
                           int b = properties["blue"].value<int>();

                           mclog::tagInfo(_tag, "led set color: r: {}, g: {}, b: {}", r, g, b);
                           GetHAL().showRgbColor(r, g, b);
                           return true;
                       });
}
