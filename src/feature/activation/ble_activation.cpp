#include "ble_activation.h"

#include <BLE.h>
#include <ESP.h>
#include <utility>

#include "app_config.h"
#include "ble_activation_protocol.h"
#include "gsmlink.h"
#include "log.h"

namespace BleActivation {
namespace {

constexpr size_t MAX_SCAN_RESULTS = 5;
constexpr size_t RESPONSE_BUFFER_SIZE = 512;
constexpr uint32_t WIFI_TEST_TIMEOUT_MS = 15000;
constexpr uint32_t MQTT_TEST_TIMEOUT_MS = 30000;
constexpr uint32_t REBOOT_DELAY_MS = 1000;

enum class State : uint8_t {
  Stopped,
  Waiting,
  Scanning,
  TestingWifi,
  TestingMqtt,
  RebootPending,
};

State state = State::Stopped;
MqttTestStartHandler startMqttTestHandler;
MqttConnectedHandler mqttConnectedHandler;
MqttStopHandler stopMqttHandler;
String candidateSsid;
String candidatePassword;
uint32_t mqttTestStartedMs = 0;
uint32_t rebootAtMs = 0;

bool timeReached(uint32_t now, uint32_t deadline) {
  return static_cast<int32_t>(now - deadline) >= 0;
}

void sendPayload(const char* payload, size_t length) {
  if (length > 0 && length < RESPONSE_BUFFER_SIZE) {
    ble_send(BLE_CH_PROVISION,
             reinterpret_cast<const uint8_t*>(payload),
             length);
  }
}

void sendStatus(const char* value) {
  char payload[RESPONSE_BUFFER_SIZE];
  sendPayload(payload, BleActivationProtocol::encodeStatus(
                           value, payload, sizeof(payload)));
}

void sendError(const char* code) {
  char payload[RESPONSE_BUFFER_SIZE];
  sendPayload(payload, BleActivationProtocol::encodeError(
                           code, payload, sizeof(payload)));
}

void clearCandidate() {
  candidateSsid = "";
  candidatePassword = "";
}

void handleProvisioningMessage(const uint8_t* data, size_t length) {
  if (state != State::Waiting) {
    sendError("busy");
    return;
  }

  BleActivationProtocol::Command command;
  if (!BleActivationProtocol::decodeCommand(data, length, command)) {
    sendError("invalid_payload");
    return;
  }

  if (command.type == BleActivationProtocol::CommandType::Scan) {
    gsmlink_wifi_scan_start();
    state = State::Scanning;
    return;
  }

  candidateSsid = command.ssid;
  candidatePassword = command.password;
  gsmlink_wifi_test_start(candidateSsid.c_str(),
                          candidatePassword.c_str(),
                          WIFI_TEST_TIMEOUT_MS);
  state = State::TestingWifi;
  sendStatus("testing_wifi");
}

void pollWifiScan() {
  GsmWifiAp accessPoints[MAX_SCAN_RESULTS] = {};
  size_t count = 0;
  if (!gsmlink_wifi_scan_poll(accessPoints, MAX_SCAN_RESULTS, &count)) {
    return;
  }

  char payload[RESPONSE_BUFFER_SIZE];
  const size_t length = BleActivationProtocol::encodeScanResult(
      accessPoints, count, payload, sizeof(payload));
  sendPayload(payload, length);
  state = State::Waiting;
}

void pollWifiTest() {
  bool success = false;
  if (!gsmlink_wifi_test_poll(&success)) {
    return;
  }

  if (!success) {
    sendError("wifi_failed");
    clearCandidate();
    state = State::Waiting;
    return;
  }

  if (!startMqttTestHandler) {
    sendError("mqtt_failed");
    clearCandidate();
    state = State::Waiting;
    return;
  }

  if (!startMqttTestHandler(candidateSsid.c_str(), candidatePassword.c_str())) {
    sendError("mqtt_failed");
    clearCandidate();
    state = State::Waiting;
    return;
  }

  mqttTestStartedMs = millis();
  state = State::TestingMqtt;
  sendStatus("testing_mqtt");
}

void finishMqttTest(bool success) {
  if (!success) {
    if (stopMqttHandler) {
      stopMqttHandler();
    }
    sendError("mqtt_failed");
    clearCandidate();
    state = State::Waiting;
    return;
  }

  gsmlink_wifi_cred_save(candidateSsid.c_str(), candidatePassword.c_str());
  sendStatus("active");
  clearCandidate();
  rebootAtMs = millis() + REBOOT_DELAY_MS;
  state = State::RebootPending;
}

void pollMqttTest() {
  if (mqttConnectedHandler && mqttConnectedHandler()) {
    finishMqttTest(true);
    return;
  }

  if (static_cast<uint32_t>(millis() - mqttTestStartedMs) >=
      MQTT_TEST_TIMEOUT_MS) {
    finishMqttTest(false);
  }
}

}  // namespace

bool hasStoredCredentials() {
  return !gsmlink_wifi_cred_ssid().isEmpty();
}

bool loadStoredCredentials(String& ssid, String& password) {
  ssid = gsmlink_wifi_cred_ssid();
  password = gsmlink_wifi_cred_pass();
  return !ssid.isEmpty();
}

void begin(MqttTestStartHandler mqttTestStartHandler,
           MqttConnectedHandler mqttConnected,
           MqttStopHandler mqttStop) {
  if (state != State::Stopped) {
    return;
  }

  startMqttTestHandler = std::move(mqttTestStartHandler);
  mqttConnectedHandler = std::move(mqttConnected);
  stopMqttHandler = std::move(mqttStop);
  ble_register_channel(BLE_CH_PROVISION, handleProvisioningMessage);
  ble_begin(AppConfig::System::BLE_DEVICE_NAME);
  state = State::Waiting;
  LOG_PRINTLN("[activation] BLE provisioning started");
}

void loop() {
  if (state == State::Stopped) {
    return;
  }

  ble_dispatch();

  if (state == State::Scanning) {
    pollWifiScan();
  } else if (state == State::TestingWifi) {
    pollWifiTest();
  } else if (state == State::TestingMqtt) {
    pollMqttTest();
  } else if (state == State::RebootPending &&
             timeReached(millis(), rebootAtMs)) {
    ESP.restart();
  }
}

}  // namespace BleActivation
