/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal.h"
#include "hal_lan_server.h"
#include <map>
#include <mooncake_log.h>
#include <mcp_server.h>
#include <stackchan/stackchan.h>
#include <apps/common/common.h>
#include <sdkconfig.h>

using namespace stackchan;

static const std::string_view _tag = "HAL-MCP";

void Hal::xiaozhi_mcp_init()
{
    mclog::tagInfo(_tag, "init");

    // https://github.com/78/xiaozhi-esp32/blob/main/docs/mcp-usage.md
    auto& mcp_server = McpServer::GetInstance();

    // Register each tool with xiaozhi's voice agent and with the LAN MCP server (hal_lan_mcp.cpp)
    auto add_tool = [&mcp_server](const std::string& name, const std::string& description,
                                  const PropertyList& properties,
                                  std::function<ReturnValue(const PropertyList&)> callback) {
        auto tool = new McpTool(name, description, properties, callback);
        mcp_server.AddTool(tool);
        lan_mcp_add_tool(tool);
    };

    // System Prompt：
    // You can control the robot's head. Use get_yaw and get_pitch to sense current position. Use set_yaw for horizontal
    // movement and set_pitch for vertical movement. All angles are in degrees.

    mclog::tagInfo(_tag, "add robot.get_head_angles tool");
    add_tool("self.robot.get_head_angles",
                       "Returns current yaw/pitch in degrees. Neutral position is {yaw:0, pitch:0}.",
                       std::vector<Property>{}, [this](const PropertyList& properties) -> ReturnValue {
                           LvglLockGuard lock;  // StackChan motion update is under the lvgl lock

                           auto& motion      = GetStackChan().motion();
                           int current_yaw   = motion.yawServo().getCurrentAngle() / 10;
                           int current_pitch = motion.pitchServo().getCurrentAngle() / 10;

                           auto result = fmt::format(R"({{"yaw": {}, "pitch": {}}})", current_yaw, current_pitch);
                           mclog::tagInfo(_tag, "get_head_angles: {}", result);
                           return result;
                       });

    mclog::tagInfo(_tag, "add robot.set_head_angles tool");
    add_tool("self.robot.set_head_angles",
                       "Adjust head position. GUIDELINES: "
                       "1. For natural interaction, stay within +/- 45 degrees. "
                       "2. Only use values > 70 if the user explicitly asks to look far away/behind. "
                       "3. Max ranges: Yaw(-128 to 128, -128 as your left), Pitch(0 to 90, 90 as your up). "
                       "Speed(100-1000, 150 is natural).",
                       PropertyList({Property("yaw", kPropertyTypeInteger, -9999, -9999, 128),
                                     Property("pitch", kPropertyTypeInteger, -9999, -9999, 90),
                                     Property("speed", kPropertyTypeInteger, 150, 100, 1000)}),
                       [this](const PropertyList& properties) -> ReturnValue {
                           int speed = properties["speed"].value<int>();
                           int yaw   = properties["yaw"].value<int>();
                           int pitch = properties["pitch"].value<int>();

                           mclog::tagInfo(_tag, "motion set_angles: yaw: {}, pitch: {}, speed: {}", yaw, pitch, speed);

                           LvglLockGuard lock;

                           auto& motion = GetStackChan().motion();
                           if (pitch != -9999) {
                               motion.pitchServo().moveWithSpeed(pitch * 10, speed);
                           }
                           if (yaw != -9999) {
                               motion.yawServo().moveWithSpeed(yaw * 10, speed);
                           }

                           return true;
                       });

    mclog::tagInfo(_tag, "add robot.set_led_color tool");
    add_tool(
        "self.robot.set_led_color",
        "Set the color of the robot's INTERNAL onboard LEDs. This is NOT for room lights. "
        "The color stays until self.robot.reset_led_color is called; until then the LEDs stop showing your mood. "
        "Values: 0-168 (safe range). Red=168,0,0; Green=0,168,0; Blue=0,0,168; White=100,100,100; Off=0,0,0.",
        PropertyList({Property("red", kPropertyTypeInteger, 0, 0, 168),
                      Property("green", kPropertyTypeInteger, 0, 0, 168),
                      Property("blue", kPropertyTypeInteger, 0, 0, 168)}),
        [this](const PropertyList& properties) -> ReturnValue {
            int r = properties["red"].value<int>();
            int g = properties["green"].value<int>();
            int b = properties["blue"].value<int>();

            mclog::tagInfo(_tag, "set_led_color: r={}, g={}, b={}", r, g, b);

            LvglLockGuard lock;

            GetStackChan().ledMood().setOverride(r, g, b);

            return true;
        });

    mclog::tagInfo(_tag, "add robot.reset_led_color tool");
    add_tool("self.robot.reset_led_color",
             "Go back to automatic LED moods (green while listening, a pulse matching your mood while speaking, off "
             "when idle). Use when the user asks to reset the lights or set them back to normal/automatic.",
             std::vector<Property>{}, [this](const PropertyList& properties) -> ReturnValue {
                 mclog::tagInfo(_tag, "reset_led_color");
                 LvglLockGuard lock;
                 GetStackChan().ledMood().clearOverride();
                 return true;
             });

    // Only one gesture or dance at a time, so a new one replaces the last
    // (modifier ids are reused once a modifier finishes, so check the slot still holds our gesture before removing)
    static int gesture_modifier_id       = -1;
    static Modifier* gesture_modifier    = nullptr;
    auto start_gesture                   = [](std::unique_ptr<Modifier> modifier) {
        auto& stackchan = GetStackChan();
        if (gesture_modifier_id >= 0 && stackchan.getModifier(gesture_modifier_id) == gesture_modifier) {
            stackchan.removeModifier(gesture_modifier_id);
        }
        gesture_modifier    = modifier.get();
        gesture_modifier_id = stackchan.addModifier(std::move(modifier));
    };

    mclog::tagInfo(_tag, "add robot.nod tool");
    add_tool("self.robot.nod", "Nod your head, e.g. to say yes or agree.",
             PropertyList({Property("times", kPropertyTypeInteger, 2, 1, 5)}),
             [this, start_gesture](const PropertyList& properties) -> ReturnValue {
                 int times = properties["times"].value<int>();
                 mclog::tagInfo(_tag, "nod: {}", times);
                 LvglLockGuard lock;
                 start_gesture(std::make_unique<HeadGestureModifier>(HeadGestureModifier::Nod(times)));
                 return true;
             });

    mclog::tagInfo(_tag, "add robot.shake_head tool");
    add_tool("self.robot.shake_head", "Shake your head, e.g. to say no or disagree.",
             PropertyList({Property("times", kPropertyTypeInteger, 2, 1, 5)}),
             [this, start_gesture](const PropertyList& properties) -> ReturnValue {
                 int times = properties["times"].value<int>();
                 mclog::tagInfo(_tag, "shake_head: {}", times);
                 LvglLockGuard lock;
                 start_gesture(std::make_unique<HeadGestureModifier>(HeadGestureModifier::Shake(times)));
                 return true;
             });

    mclog::tagInfo(_tag, "add robot.dance tool");
    add_tool("self.robot.dance",
             "Do a short dance (a few seconds). Styles: happy (sway side to side), robot (stiff sharp moves), "
             "panic (fast shaking), look_around (slowly scan the room).",
             PropertyList({Property("style", kPropertyTypeString, std::string("happy"))}),
             [this, start_gesture](const PropertyList& properties) -> ReturnValue {
                 static const std::map<std::string, const animation::KeyframeSequence*> styles = {
                     {"happy", &DanceModifier::Happy},
                     {"robot", &DanceModifier::Robot},
                     {"panic", &DanceModifier::Panic},
                     {"look_around", &DanceModifier::LookAround}};

                 auto style = properties["style"].value<std::string>();
                 auto it    = styles.find(style);
                 if (it == styles.end()) {
                     throw std::invalid_argument("Unknown dance style: " + style);
                 }
                 mclog::tagInfo(_tag, "dance: {}", style);
                 LvglLockGuard lock;
                 start_gesture(std::make_unique<DanceModifier>(*it->second));
                 return true;
             });

    mclog::tagInfo(_tag, "add robot.create_reminder tool");
    add_tool("self.robot.create_reminder",
                       "Create a reminder. Duration is in seconds. Message is what to say when time is up. Set repeat "
                       "to true to repeat the reminder.",
                       PropertyList({Property("duration_seconds", kPropertyTypeInteger, 60, 1, 86400),
                                     Property("message", kPropertyTypeString, std::string("Time's up!")),
                                     Property("repeat", kPropertyTypeBoolean, false)}),
                       [this](const PropertyList& properties) -> ReturnValue {
                           int duration_seconds = properties["duration_seconds"].value<int>();
                           std::string message  = properties["message"].value<std::string>();
                           bool repeat          = properties["repeat"].value<bool>();

                           // Default message
                           if (message.empty()) {
                               message = "Time's up!";
                           }

                           mclog::tagInfo(_tag, "create_reminder: duration={}s, message={}, repeat={}",
                                          duration_seconds, message, repeat);

                           int id = tools::create_reminder(duration_seconds * 1000, message, repeat);

                           return id;
                       });

    mclog::tagInfo(_tag, "add robot.get_reminders tool");
    add_tool("self.robot.get_reminders", "Get list of active reminders.", std::vector<Property>{},
                       [this](const PropertyList& properties) -> ReturnValue {
                           mclog::tagInfo(_tag, "get_reminders");
                           auto reminders          = tools::get_active_reminders();
                           std::string result_json = "[";
                           for (size_t i = 0; i < reminders.size(); ++i) {
                               const auto& r = reminders[i];
                               result_json +=
                                   fmt::format(R"({{"id": {}, "duration_ms": {}, "message": "{}", "repeat": {}}})",
                                               r.id, r.durationMs, r.message, r.repeat ? "true" : "false");
                               if (i < reminders.size() - 1) {
                                   result_json += ", ";
                               }
                           }
                           result_json += "]";
                           mclog::tagInfo(_tag, "get_reminders result: {}", result_json);
                           return result_json;
                       });

    mclog::tagInfo(_tag, "add robot.stop_reminder tool");
    add_tool("self.robot.stop_reminder", "Stop a reminder by ID.",
                       PropertyList({Property("id", kPropertyTypeInteger, -1)}),
                       [this](const PropertyList& properties) -> ReturnValue {
                           int id = properties["id"].value<int>();
                           mclog::tagInfo(_tag, "stop_reminder: id={}", id);
                           tools::stop_reminder(id);
                           return true;
                       });

    mclog::tagInfo(_tag, "add robot.set_expression tool");
    add_tool("self.robot.set_expression",
             "Show a facial expression for a few seconds. "
             "Expressions: neutral, happy, angry, sad, doubt, sleepy.",
             PropertyList({Property("expression", kPropertyTypeString, std::string("happy")),
                           Property("seconds", kPropertyTypeInteger, 3, 1, 60)}),
             [this](const PropertyList& properties) -> ReturnValue {
                 static const std::map<std::string, avatar::Emotion> emotions = {
                     {"neutral", avatar::Emotion::Neutral}, {"happy", avatar::Emotion::Happy},
                     {"angry", avatar::Emotion::Angry},     {"sad", avatar::Emotion::Sad},
                     {"doubt", avatar::Emotion::Doubt},     {"sleepy", avatar::Emotion::Sleepy}};

                 auto expression = properties["expression"].value<std::string>();
                 auto emotion    = emotions.find(expression);
                 if (emotion == emotions.end()) {
                     throw std::invalid_argument("Unknown expression: " + expression);
                 }
                 int seconds = properties["seconds"].value<int>();
                 mclog::tagInfo(_tag, "set_expression: {} for {}s", expression, seconds);

                 LvglLockGuard lock;
                 GetStackChan().addModifier(std::make_unique<TimedEmotionModifier>(emotion->second, seconds * 1000));
                 return true;
             });

    mclog::tagInfo(_tag, "add robot.show_message tool");
    add_tool("self.robot.show_message",
             "Show a short message in a speech bubble on your face, e.g. to reply to someone who texted you.",
             PropertyList({Property("text", kPropertyTypeString, std::string("")),
                           Property("seconds", kPropertyTypeInteger, 6, 1, 60)}),
             [this](const PropertyList& properties) -> ReturnValue {
                 auto text   = properties["text"].value<std::string>();
                 int seconds = properties["seconds"].value<int>();
                 mclog::tagInfo(_tag, "show_message: {}", text);

                 LvglLockGuard lock;
                 GetStackChan().addModifier(std::make_unique<TimedSpeechModifier>(text, seconds * 1000));
                 GetStackChan().addModifier(std::make_unique<SpeakingModifier>(std::min(seconds, 3) * 1000));
                 return true;
             });

#if CONFIG_STACKCHAN_REEFWATCH_ENABLE
    mclog::tagInfo(_tag, "add cameras.show_live tool");
    add_tool("self.cameras.show_live",
                       "Show a home security camera live on your screen for up to 5 minutes. "
                       "Use when the user asks to see or check a camera. Camera names are like 'front' or 'garden'.",
                       PropertyList({Property("camera", kPropertyTypeString, std::string("front"))}),
                       [this](const PropertyList& properties) -> ReturnValue {
                           auto camera = properties["camera"].value<std::string>();
                           mclog::tagInfo(_tag, "cameras.show_live: {}", camera);
                           setReefwatchLiveCamera(camera);
                           return true;
                       });

    mclog::tagInfo(_tag, "add cameras.stop_live tool");
    add_tool("self.cameras.stop_live", "Stop showing the live camera view.", std::vector<Property>{},
                       [this](const PropertyList& properties) -> ReturnValue {
                           mclog::tagInfo(_tag, "cameras.stop_live");
                           setReefwatchLiveCamera("");
                           return true;
                       });
#endif
}
