#include "mqtt_connection.h"

#include <PubSubClient.h>
#include <WiFiClientSecure.h>
#include <memory>
#include <utility>

#include "app_config.h"
#include "gsmlink.h"
#include "log.h"
#include "secrets.h"

namespace MqttConnection {
namespace {

WiFiClientSecure secureClient;
PubSubClient mqttClient(secureClient);
std::unique_ptr<GsmLink> wifiLink;
MessageHandler incomingMessageHandler;
SessionHandler connectedSessionHandler;

const char* mqttWillTopic = nullptr;
uint32_t lastWifiAttemptMs = 0;
uint32_t lastMqttAttemptMs = 0;
bool initialized = false;

bool intervalElapsed(uint32_t now, uint32_t previous, uint32_t interval) {
  return static_cast<uint32_t>(now - previous) >= interval;
}

void mqttCallback(char* topic, uint8_t* payload, unsigned int length) {
  if (incomingMessageHandler) {
    incomingMessageHandler(topic, payload, length);
  }
}

void connectWifi(uint32_t now) {
  if (!wifiLink) {
    return;
  }

  lastWifiAttemptMs = now;
  LOG_PRINTLN("[wifi] Connecting with stored credentials");
  wifiLink->waitForNetwork(1);
}

void maintainWifi(uint32_t now) {
  if (wifiLink && wifiLink->isGprsConnected()) {
    return;
  }

  if (mqttClient.connected()) {
    mqttClient.disconnect();
  }

  if (intervalElapsed(now, lastWifiAttemptMs,
                      AppConfig::Network::WIFI_RECONNECT_INTERVAL_MS)) {
    connectWifi(now);
  }
}

void connectMqtt(uint32_t now) {
  lastMqttAttemptMs = now;

  const bool connected = mqttClient.connect(
      AppConfig::Network::DEVICE_ID,
      Secrets::MQTT_USERNAME,
      Secrets::MQTT_PASSWORD,
      mqttWillTopic,
      1,
      true,
      "offline");

  if (!connected) {
    LOG_PRINTF("[mqtt] Connection failed, state=%d\n", mqttClient.state());
    return;
  }

  if (!connectedSessionHandler || !connectedSessionHandler()) {
    LOG_PRINTLN("[mqtt] Session initialization failed");
    mqttClient.disconnect();
    return;
  }

  LOG_PRINTLN("[mqtt] Connected and subscribed");
}

void maintainMqtt(uint32_t now) {
  if (!wifiLink || !wifiLink->isGprsConnected() || mqttClient.connected()) {
    return;
  }

  if (intervalElapsed(now, lastMqttAttemptMs,
                      AppConfig::Network::MQTT_RECONNECT_INTERVAL_MS)) {
    connectMqtt(now);
  }
}

}  // namespace

bool begin(const char* ssid,
           const char* password,
           const char* willTopic,
           MessageHandler messageHandler,
           SessionHandler sessionHandler) {
  if (ssid == nullptr || ssid[0] == '\0' || password == nullptr ||
      willTopic == nullptr) {
    return false;
  }

  stop();
  wifiLink.reset(new GsmLink(ssid, password));
  mqttWillTopic = willTopic;
  incomingMessageHandler = std::move(messageHandler);
  connectedSessionHandler = std::move(sessionHandler);

  if (AppConfig::Network::MQTT_TLS_INSECURE) {
    secureClient.setInsecure();
    LOG_PRINTLN("[mqtt] WARNING: TLS certificate verification is disabled");
  }

  mqttClient.setServer(AppConfig::Network::MQTT_HOST, AppConfig::Network::MQTT_PORT);
  mqttClient.setCallback(mqttCallback);
  mqttClient.setBufferSize(AppConfig::Network::MQTT_PACKET_BUFFER_SIZE);
  mqttClient.setKeepAlive(30);
  mqttClient.setSocketTimeout(5);

  initialized = true;
  connectWifi(millis());
  return true;
}

void loop() {
  if (!initialized) {
    return;
  }

  const uint32_t now = millis();
  maintainWifi(now);
  maintainMqtt(now);

  if (mqttClient.connected()) {
    mqttClient.loop();
  }
}

void stop() {
  initialized = false;

  if (mqttClient.connected()) {
    if (mqttWillTopic != nullptr) {
      mqttClient.publish(mqttWillTopic, "offline", true);
    }
    mqttClient.disconnect();
  }

  WiFi.disconnect(true);
  wifiLink.reset();
  incomingMessageHandler = nullptr;
  connectedSessionHandler = nullptr;
  mqttWillTopic = nullptr;
  lastWifiAttemptMs = 0;
  lastMqttAttemptMs = 0;
}

bool subscribe(const char* topic, uint8_t qos) {
  return mqttClient.connected() && mqttClient.subscribe(topic, qos);
}

bool publish(const char* topic,
             const uint8_t* payload,
             size_t length,
             bool retained) {
  if (!mqttClient.connected() || payload == nullptr) {
    return false;
  }

  return mqttClient.publish(topic,
                            payload,
                            static_cast<unsigned int>(length),
                            retained);
}

bool publish(const char* topic, const char* payload, bool retained) {
  return mqttClient.connected() && mqttClient.publish(topic, payload, retained);
}

bool isConnected() {
  return mqttClient.connected();
}

}  // namespace MqttConnection
