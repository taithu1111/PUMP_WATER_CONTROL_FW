#include <Arduino.h>

#include "feature/activation/ble_activation.h"
#include "feature/automation/relay_automation.h"
#include "feature/mqtt/mqtt_relay_gateway.h"
#include "feature/relay/relay_feature.h"
#include "log.h"

namespace {

bool startMqtt(const char* ssid, const char* password) {
  return MqttRelayGateway::begin(
      ssid, password, RelayAutomation::handleCommand,
      RelayAutomation::getSnapshot, RelayFeature::getAll);
}

}  // namespace

void setup() {
  Serial.begin(115200);
  LOG_PRINTLN("[main] Starting pump controller");

  if (!RelayFeature::begin()) {
    LOG_PRINTLN("[main] Relay initialization failed; network will not start");
    return;
  }
  if (!RelayAutomation::begin(RelayFeature::setChannel,
                              MqttRelayGateway::notifyStateChange)) {
    LOG_PRINTLN("[main] Automation initialization failed; network will not start");
    return;
  }

  String ssid;
  String password;
  if (BleActivation::loadStoredCredentials(ssid, password)) {
    LOG_PRINTLN("[main] Stored Wi-Fi credentials found; BLE remains off");
    if (!startMqtt(ssid.c_str(), password.c_str()))
      LOG_PRINTLN("[main] MQTT startup failed");
    return;
  }

  LOG_PRINTLN("[main] No Wi-Fi credentials; starting BLE provisioning");
  BleActivation::begin(startMqtt, MqttRelayGateway::isConnected,
                       MqttRelayGateway::stop);
}

void loop() {
  RelayAutomation::loop();
  MqttRelayGateway::loop();
  BleActivation::loop();
}
