#pragma once

#include <Arduino.h>
#include <functional>

namespace MqttConnection {

using MessageHandler =
    std::function<void(const char* topic, const uint8_t* payload, size_t length)>;
using SessionHandler = std::function<bool()>;

bool begin(const char* ssid,
           const char* password,
           const char* willTopic,
           MessageHandler messageHandler,
           SessionHandler sessionHandler);
void loop();
void stop();

bool subscribe(const char* topic, uint8_t qos = 0);
bool publish(const char* topic,
             const uint8_t* payload,
             size_t length,
             bool retained);
bool publish(const char* topic, const char* payload, bool retained);
bool isConnected();

}  // namespace MqttConnection
