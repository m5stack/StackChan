/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal.h"
#include <stackchan/stackchan.h>
#include <mooncake.h>
#include <mooncake_log.h>
#include <ArduinoJson.hpp>
#include <cstdint>
#include <board.h>
#include <mqtt.h>
#include <esp_log.h>
#include <mutex>
#include <queue>
#include <vector>
#include <map>

using namespace stackchan;

static std::string _tag = "EzData";

class EzData {
public:
    enum class CmdType : int {
        // --- 数据操作 (100-102) ---
        DeviceAddData    = 100,  ///< 设备端新增数据
        DeviceUpdateData = 101,  ///< 设备端修改数据
        DeviceDeleteData = 102,  ///< 设备端删除数据

        // --- 数据查询 (103-104) ---
        DeviceQueryList   = 103,  ///< 设备端查询数据列表
        DeviceQueryDetail = 104,  ///< 设备端查询数据详情

        // --- 文件上传 (105) ---
        DeviceUploadFile = 105,  ///< 设备端上传文件 (通知)

        // --- 用户操作 (106-109) ---
        UserScanCode   = 106,  ///< 用户端扫码
        UserUpdateData = 107,  ///< 用户端修改数据
        UserDeleteData = 108,  ///< 用户端删除数据
        UserAddData    = 109,  ///< 用户端新增数据

        // --- 设备操作 (112) ---
        DeviceGetMatchCode = 112,  ///< 设备端获取匹配码

        // --- 错误 (500) ---
        Error = 500  ///< 设备端请求错误
    };

    std::function<void(void)> onConnected;
    std::function<void(std::string_view)> onPairCodeReceived;
    std::function<void(std::string_view, const ArduinoJson::JsonVariant&)> onUserUpdateData;

    void init()
    {
        _connect();
    }

    void update()
    {
        if (!_mqtt) {
            return;
        }

        if (!_mqtt->IsConnected()) {
            if (GetHAL().millis() - _last_reconnect_attempt > 5000) {
                ESP_LOGI(_tag.c_str(), "Reconnecting...");
                _connect();
            }
        } else {
            _process_messages();
            _check_callbacks_timeout();
        }
    }

    void sendPacket(CmdType type, const std::function<void(ArduinoJson::JsonObject&)>& bodyBuilder = nullptr)
    {
        if (!_mqtt || !_mqtt->IsConnected()) {
            return;
        }

        ArduinoJson::JsonDocument doc;
        doc["deviceToken"] = _token;

        ArduinoJson::JsonObject body = doc["body"].to<ArduinoJson::JsonObject>();
        body["requestType"]          = static_cast<int>(type);

        if (bodyBuilder) {
            bodyBuilder(body);
        }

        std::string payload;
        ArduinoJson::serializeJson(doc, payload);

        ESP_LOGI(_tag.c_str(), "Sending Packet Type: %d", (int)type);
        _mqtt->Publish(_pub_topic, payload, 0);
    }

    void requestPairCode()
    {
        ESP_LOGI(_tag.c_str(), "Requesting pair code");
        sendPacket(CmdType::DeviceGetMatchCode);
    }

    template <typename T>
    void addData(std::string_view name, T value)
    {
        sendPacket(CmdType::DeviceAddData, [&name, &value](ArduinoJson::JsonObject& body) {
            body["name"]  = name;
            body["value"] = value;
        });
    }

    template <typename T>
    void modifyData(std::string_view name, T value)
    {
        sendPacket(CmdType::DeviceUpdateData, [&name, &value](ArduinoJson::JsonObject& body) {
            body["name"]  = name;
            body["value"] = value;
        });
    }

    void getData(std::string_view name, std::function<void(const ArduinoJson::JsonVariant&)> onData,
                 std::function<void()> onFailed)
    {
        // 注册回调
        {
            std::lock_guard<std::mutex> lock(_callbacks_mutex);
            _get_data_callbacks[name.data()] = {onData, onFailed, GetHAL().millis()};
        }

        // 发送请求
        sendPacket(CmdType::DeviceQueryDetail, [&name](ArduinoJson::JsonObject& body) { body["name"] = name; });
    }

private:
    std::unique_ptr<Mqtt> _mqtt;
    uint32_t _last_reconnect_attempt = 0;
    std::string _sub_topic;
    std::string _pub_topic;
    std::string _token;

    std::mutex _mutex;
    std::queue<std::string> _msg_queue;

    struct GetDataCallback_t {
        std::function<void(const ArduinoJson::JsonVariant&)> onData;
        std::function<void()> onFailed;
        uint32_t requestTime;
    };
    std::mutex _callbacks_mutex;
    std::map<std::string, GetDataCallback_t> _get_data_callbacks;

    std::string _get_device_token()
    {
        const std::string url = "https://ezdata2.m5stack.com/api/v2/device/registerMac";

        auto& board  = Board::GetInstance();
        auto network = board.GetNetwork();

        auto http = network->CreateHttp(0);
        if (!http) {
            ESP_LOGE(_tag.c_str(), "failed to create HTTP instance");
            return "";
        }

        ArduinoJson::JsonDocument doc;
        doc["deviceType"] = "CoreS3";
        doc["mac"]        = GetHAL().getFactoryMacString();

        std::string payload;
        ArduinoJson::serializeJson(doc, payload);

        http->SetHeader("Content-Type", "application/json");
        http->SetContent(std::move(payload));

        if (!http->Open("POST", url)) {
            ESP_LOGE(_tag.c_str(), "failed to open HTTP connection");
            return "";
        }

        int status_code = http->GetStatusCode();

        if (status_code != 200) {
            ESP_LOGE(_tag.c_str(), "HTTP request failed, status code: %d", status_code);
            http->Close();
            return "";
        }

        std::string response = http->ReadAll();
        http->Close();

        doc.clear();
        auto error = ArduinoJson::deserializeJson(doc, response);
        if (error) {
            ESP_LOGE(_tag.c_str(), "failed to parse JSON response: %s", error.c_str());
            return "";
        }

        if (doc["code"] == 200 && doc["msg"] == "OK") {
            if (doc["data"].is<std::string>()) {
                return doc["data"].as<std::string>();
            }
        }

        ESP_LOGE(_tag.c_str(), "failed to get token from response");
        return "";
    }

    void _connect()
    {
        _mqtt.reset();

        _token = _get_device_token();
        if (_token.empty()) {
            ESP_LOGE(_tag.c_str(), "failed to get device token");
            _last_reconnect_attempt = GetHAL().millis();
            return;
        }
        ESP_LOGI(_tag.c_str(), "get token %s", _token.c_str());

        auto& board  = Board::GetInstance();
        auto network = board.GetNetwork();

        _mqtt = network->CreateMqtt(1);
        if (!_mqtt) {
            ESP_LOGE(_tag.c_str(), "Failed to create MQTT instance");
            _last_reconnect_attempt = GetHAL().millis();
            return;
        }

        std::string mac       = GetHAL().getFactoryMacString();
        std::string client_id = fmt::format("ez{}ez", mac);
        _sub_topic            = fmt::format("$ezdata/{}/down", _token);
        _pub_topic            = fmt::format("$ezdata/{}/up", _token);

        _mqtt->OnConnected([this]() {
            ESP_LOGI(_tag.c_str(), "Connected");
            _mqtt->Subscribe(_sub_topic, 0);
            ESP_LOGI(_tag.c_str(), "Subscribed: %s", _sub_topic.c_str());

            if (onConnected) {
                onConnected();
            }
        });

        _mqtt->OnDisconnected([this]() { ESP_LOGI(_tag.c_str(), "Disconnected"); });

        _mqtt->OnMessage([this](const std::string&, const std::string& payload) {
            std::lock_guard<std::mutex> lock(_mutex);
            _msg_queue.push(payload);
        });

        ESP_LOGI(_tag.c_str(), "Connecting to EzData as %s", client_id.c_str());
        _mqtt->Connect("uiflow2.m5stack.com", 1883, client_id, _token, "");

        _last_reconnect_attempt = GetHAL().millis();
    }

    void _check_callbacks_timeout()
    {
        std::lock_guard<std::mutex> lock(_callbacks_mutex);
        uint32_t now = GetHAL().millis();
        for (auto it = _get_data_callbacks.begin(); it != _get_data_callbacks.end();) {
            if (now - it->second.requestTime > 5000) {
                ESP_LOGW(_tag.c_str(), "GetData timeout for %s", it->first.c_str());
                if (it->second.onFailed) {
                    it->second.onFailed();
                }
                it = _get_data_callbacks.erase(it);
            } else {
                ++it;
            }
        }
    }

    void _process_messages()
    {
        std::vector<std::string> messages;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            while (!_msg_queue.empty()) {
                messages.push_back(std::move(_msg_queue.front()));
                _msg_queue.pop();
            }
        }

        for (const auto& msg : messages) {
            _handle_message(msg);
        }
    }

    void _handle_message(std::string_view payload)
    {
        ArduinoJson::JsonDocument doc;
        auto error = ArduinoJson::deserializeJson(doc, payload);
        if (error) {
            ESP_LOGE(_tag.c_str(), "DeserializeJson failed: %s", error.c_str());
            return;
        }

        if (!doc["code"].is<int>()) {
            return;
        }

        int code = doc["code"];
        if (code != 200) {
            ESP_LOGW(_tag.c_str(), "EzData response error code: %d", code);
            return;
        }

        if (!doc["cmd"].is<int>()) {
            return;
        }
        CmdType cmd = static_cast<CmdType>(doc["cmd"].as<int>());
        // auto body = doc["body"];

        ESP_LOGI(_tag.c_str(), "Received Cmd: %d", (int)cmd);

        switch (cmd) {
            case CmdType::DeviceQueryList:
                ESP_LOGI(_tag.c_str(), "Device list received");
                break;
            case CmdType::DeviceGetMatchCode: {
                if (doc["body"]["pairCode"].is<std::string>()) {
                    std::string pair_code = doc["body"]["pairCode"].as<std::string>();
                    ESP_LOGI(_tag.c_str(), "Pair code: %s", pair_code.c_str());

                    if (onPairCodeReceived) {
                        onPairCodeReceived(pair_code);
                    }
                }
                break;
            }
            case CmdType::DeviceQueryDetail: {
                // {"body": {"createTime": 1751274100000, "dataToken": "...", "id": "...", "name": "adasd",
                // "updateTime": 1751274100000, "value": "ddsad"}}
                if (doc["body"].is<ArduinoJson::JsonObject>()) {
                    std::string name;
                    ArduinoJson::JsonVariant value = doc["body"]["value"];

                    if (doc["body"]["name"].is<std::string>()) {
                        name = doc["body"]["name"].as<std::string>();
                    }

                    if (!name.empty()) {
                        std::lock_guard<std::mutex> lock(_callbacks_mutex);
                        auto it = _get_data_callbacks.find(name);
                        if (it != _get_data_callbacks.end()) {
                            if (it->second.onData) {
                                it->second.onData(value);
                            }
                            _get_data_callbacks.erase(it);
                        }
                    }
                }
                break;
            }
            case CmdType::UserUpdateData: {
                if (doc["body"].is<ArduinoJson::JsonObject>()) {
                    std::string name;
                    ArduinoJson::JsonVariant value = doc["body"]["value"];

                    if (doc["body"]["name"].is<std::string>()) {
                        name = doc["body"]["name"].as<std::string>();
                    }

                    if (!name.empty() && onUserUpdateData) {
                        onUserUpdateData(name, value);
                    }
                }
                break;
            }
            default:
                break;
        }
    }
};

class EzdataWorker : public mooncake::BasicAbility {
public:
    EzdataWorker()
    {
        _service = std::make_unique<EzData>();

        _service->onConnected        = [this]() { _service->requestPairCode(); };
        _service->onPairCodeReceived = [this](std::string_view pairCode) { GetHAL().onEzdataPairCode.emit(pairCode); };
        _service->onUserUpdateData   = [this](std::string_view name, const ArduinoJson::JsonVariant& value) {
            handle_user_update_data(name, value);
        };

        _service->init();

        setup_data();
    }

    void onRunning() override
    {
        _service->update();
    }

    void onDestroy() override
    {
        _service.reset();
    }

private:
    std::unique_ptr<EzData> _service;
    int _pitch_servo_speed = 500;
    int _yaw_servo_speed   = 500;

    const std::string_view KEY_PITCH_SERVO_ANGLE = "SERVO.Y.ANGLE";
    const std::string_view KEY_PITCH_SERVO_SPEED = "SERVO.Y.SPEED";
    const std::string_view KEY_YAW_SERVO_ANGLE   = "SERVO.X.ANGLE";
    const std::string_view KEY_YAW_SERVO_SPEED   = "SERVO.X.SPEED";

    void setup_data()
    {
        auto& motion = GetStackChan().motion();

        _service->addData(KEY_PITCH_SERVO_ANGLE, ((float)motion.getCurrentPitchAngle() / 10.0f));
        _service->addData(KEY_YAW_SERVO_ANGLE, ((float)motion.getCurrentYawAngle() / 10.0f));
        _service->addData(KEY_PITCH_SERVO_SPEED, _pitch_servo_speed);
        _service->addData(KEY_YAW_SERVO_SPEED, _yaw_servo_speed);
    }

    void handle_user_update_data(std::string_view name, const ArduinoJson::JsonVariant& value)
    {
        mclog::tagInfo(_tag, "on user update data {}", name);

        auto& motion = GetStackChan().motion();

        if (name == KEY_PITCH_SERVO_ANGLE) {
            if (value.is<float>()) {
                float angle = value.as<float>();
                motion.movePitchWithSpeed(angle * 10, _pitch_servo_speed);
            }
        } else if (name == KEY_YAW_SERVO_ANGLE) {
            if (value.is<float>()) {
                float angle = value.as<float>();
                motion.moveYawWithSpeed(angle * 10, _yaw_servo_speed);
            }
        } else if (name == KEY_PITCH_SERVO_SPEED) {
            if (value.is<int>()) {
                _pitch_servo_speed = value.as<int>();
            }
        } else if (name == KEY_YAW_SERVO_SPEED) {
            if (value.is<int>()) {
                _yaw_servo_speed = value.as<int>();
            }
        }
    }
};

void Hal::startEzDataService(std::function<void(std::string_view)> onStartLog)
{
    mclog::tagInfo(_tag, "start ezdata service");

    startNetwork(onStartLog);

    onStartLog("Connecting to\nserver...");
    mooncake::GetMooncake().extensionManager()->createAbility(std::make_unique<EzdataWorker>());
}
