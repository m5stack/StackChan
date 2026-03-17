/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "json_helper.h"
#include <ArduinoJson.hpp>
#include <mooncake_log.h>

static const char* _tag = "stackchan-json";

using namespace uitk;

namespace stackchan::avatar {

static void update_feature(Feature& feature, ArduinoJson::JsonObject& jsonObject)
{
    if (jsonObject["x"].is<int>() && jsonObject["y"].is<int>()) {
        Vector2i position;
        position.x = jsonObject["x"];
        position.y = jsonObject["y"];
        feature.setPosition(position);
    }

    if (jsonObject["rotation"].is<int>()) {
        int rotation = jsonObject["rotation"];
        feature.setRotation(rotation);
    }

    if (jsonObject["weight"].is<int>()) {
        int weight = jsonObject["weight"];
        feature.setWeight(weight);
    }

    if (jsonObject["size"].is<int>()) {
        int size = jsonObject["size"];
        feature.setSize(size);
    }
}

void update_from_json(Avatar* avatar, const char* jsonContent)
{
    if (!avatar || !jsonContent) {
        return;
    }

    ArduinoJson::JsonDocument doc;
    auto error = ArduinoJson::deserializeJson(doc, jsonContent);
    if (error) {
        mclog::tagError(_tag, "deserializeJson failed: {}", error.c_str());
        return;
    }

    if (doc["leftEye"].is<ArduinoJson::JsonObject>()) {
        ArduinoJson::JsonObject leftEyeObj = doc["leftEye"];
        update_feature(avatar->leftEye(), leftEyeObj);
    }

    if (doc["rightEye"].is<ArduinoJson::JsonObject>()) {
        ArduinoJson::JsonObject rightEyeObj = doc["rightEye"];
        update_feature(avatar->rightEye(), rightEyeObj);
    }

    if (doc["mouth"].is<ArduinoJson::JsonObject>()) {
        ArduinoJson::JsonObject mouthObj = doc["mouth"];
        update_feature(avatar->mouth(), mouthObj);
    }
}

}  // namespace stackchan::avatar

namespace stackchan::motion {

static void update_servo(Servo& servo, ArduinoJson::JsonObject& jsonObject)
{
    // If is rotate mode
    if (jsonObject["rotate"].is<int>()) {
        int rotate = jsonObject["rotate"];
        servo.rotate(rotate);
        return;
    }

    if (!jsonObject["angle"].is<int>()) {
        return;
    }
    int angle = jsonObject["angle"];

    // If has speed, move directly
    if (jsonObject["speed"].is<int>()) {
        int speed = jsonObject["speed"];
        servo.moveWithSpeed(angle, speed);
        return;
    }

    // If has spring params, move with spring params
    if (jsonObject["spring"].is<ArduinoJson::JsonObject>()) {
        ArduinoJson::JsonObject spring = jsonObject["spring"];

        float stiffness = 170.0f;
        if (spring["stiffness"].is<float>()) {
            stiffness = spring["stiffness"];
        }

        float damping = 26.0f;
        if (spring["damping"].is<float>()) {
            damping = spring["damping"];
        }

        servo.moveWithSpringParams(angle, stiffness, damping);
        return;
    }

    // Move in deafult spring
    servo.move(angle);
}

void update_from_json(Motion* motion, const char* jsonContent)
{
    if (!motion || !jsonContent) {
        return;
    }

    ArduinoJson::JsonDocument doc;
    auto error = ArduinoJson::deserializeJson(doc, jsonContent);
    if (error) {
        mclog::tagError(_tag, "deserializeJson failed: {}", error.c_str());
        return;
    }

    if (doc["yawServo"].is<ArduinoJson::JsonObject>()) {
        ArduinoJson::JsonObject yawServoObj = doc["yawServo"];
        update_servo(motion->yawServo(), yawServoObj);
    }

    if (doc["pitchServo"].is<ArduinoJson::JsonObject>()) {
        ArduinoJson::JsonObject pitchServoObj = doc["pitchServo"];
        update_servo(motion->pitchServo(), pitchServoObj);
    }
}

}  // namespace stackchan::motion

namespace stackchan::animation {

static FeatureKeyframe parse_feature(ArduinoJson::JsonObject jsonObject)
{
    FeatureKeyframe kf;
    if (jsonObject["x"].is<int>()) {
        kf.position.x = jsonObject["x"];
    }
    if (jsonObject["y"].is<int>()) {
        kf.position.y = jsonObject["y"];
    }
    if (jsonObject["rotation"].is<int>()) {
        kf.rotation = jsonObject["rotation"];
    }
    if (jsonObject["weight"].is<int>()) {
        kf.weight = jsonObject["weight"];
    }
    if (jsonObject["size"].is<int>()) {
        kf.size = jsonObject["size"];
    }
    return kf;
}

static ServoKeyframe parse_servo(ArduinoJson::JsonObject jsonObject)
{
    ServoKeyframe kf;
    if (jsonObject["angle"].is<int>()) {
        kf.angle = jsonObject["angle"];
    }
    if (jsonObject["speed"].is<int>()) {
        kf.speed = jsonObject["speed"];
    }
    return kf;
}

KeyframeSequence parse_sequence_from_json(const char* jsonContent)
{
    KeyframeSequence sequence;
    if (!jsonContent) {
        return sequence;
    }

    ArduinoJson::JsonDocument doc;
    auto error = ArduinoJson::deserializeJson(doc, jsonContent);
    if (error) {
        mclog::tagError(_tag, "deserializeJson failed: {}", error.c_str());
        return sequence;
    }

    if (!doc.is<ArduinoJson::JsonArray>()) {
        mclog::tagError(_tag, "json is not an array");
        return sequence;
    }

    ArduinoJson::JsonArray array = doc.as<ArduinoJson::JsonArray>();
    for (ArduinoJson::JsonObject kfObj : array) {
        Keyframe kf;

        if (kfObj["leftEye"].is<ArduinoJson::JsonObject>()) {
            kf.leftEye = parse_feature(kfObj["leftEye"]);
        }
        if (kfObj["rightEye"].is<ArduinoJson::JsonObject>()) {
            kf.rightEye = parse_feature(kfObj["rightEye"]);
        }
        if (kfObj["mouth"].is<ArduinoJson::JsonObject>()) {
            kf.mouth = parse_feature(kfObj["mouth"]);
        }
        if (kfObj["yawServo"].is<ArduinoJson::JsonObject>()) {
            kf.yawServo = parse_servo(kfObj["yawServo"]);
        }
        if (kfObj["pitchServo"].is<ArduinoJson::JsonObject>()) {
            kf.pitchServo = parse_servo(kfObj["pitchServo"]);
        }
        if (kfObj["leftRgbColor"].is<const char*>()) {
            kf.leftRgbColor = kfObj["leftRgbColor"].as<std::string>();
        }
        if (kfObj["rightRgbColor"].is<const char*>()) {
            kf.rightRgbColor = kfObj["rightRgbColor"].as<std::string>();
        }
        if (kfObj["durationMs"].is<int>()) {
            kf.durationMs = kfObj["durationMs"];
        }

        sequence.push_back(kf);
    }

    return sequence;
}

}  // namespace stackchan::animation

namespace stackchan::addon {

void update_neon_light_from_json(NeonLight* left, NeonLight* right, const char* jsonContent)
{
    if (!left || !right || !jsonContent) {
        return;
    }

    ArduinoJson::JsonDocument doc;
    auto error = ArduinoJson::deserializeJson(doc, jsonContent);
    if (error) {
        mclog::tagError(_tag, "deserializeJson failed: {}", error.c_str());
        return;
    }

    if (doc["leftRgbDuration"].is<float>()) {
        left->setDuration(doc["leftRgbDuration"].as<float>());
    }
    if (doc["leftRgbColor"].is<std::string>()) {
        left->setColor(doc["leftRgbColor"].as<std::string>());
    }

    if (doc["rightRgbDuration"].is<float>()) {
        right->setDuration(doc["rightRgbDuration"].as<float>());
    }
    if (doc["rightRgbColor"].is<std::string>()) {
        right->setColor(doc["rightRgbColor"].as<std::string>());
    }
}

}  // namespace stackchan::addon
