#pragma once

#include <Arduino.h>
#include <functional>

namespace BleActivation {

using MqttTestStartHandler =
    std::function<bool(const char* ssid, const char* password)>;
using MqttConnectedHandler = std::function<bool()>;
using MqttStopHandler = std::function<void()>;

bool hasStoredCredentials();
bool loadStoredCredentials(String& ssid, String& password);

void begin(MqttTestStartHandler mqttTestStartHandler,
           MqttConnectedHandler mqttConnectedHandler,
           MqttStopHandler mqttStopHandler);
void loop();

}  // namespace BleActivation
