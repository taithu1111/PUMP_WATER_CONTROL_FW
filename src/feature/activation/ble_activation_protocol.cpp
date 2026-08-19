#include "ble_activation_protocol.h"

#include <ArduinoJson.h>

namespace BleActivationProtocol {

bool decodeCommand(const uint8_t* data, size_t length, Command& command) {
  if (data == nullptr || length == 0) {
    return false;
  }

  JsonDocument document;
  if (deserializeJson(document, data, length)) {
    return false;
  }

  const char* type = document["type"] | "";
  if (strcmp(type, "scan") == 0) {
    command.type = CommandType::Scan;
    command.ssid = "";
    command.password = "";
    return true;
  }

  if (strcmp(type, "wifi") != 0 ||
      !document["ssid"].is<const char*>() ||
      !document["password"].is<const char*>()) {
    return false;
  }

  command.ssid = document["ssid"].as<const char*>();
  command.password = document["password"].as<const char*>();
  if (command.ssid.length() == 0 || command.ssid.length() > 32 ||
      command.password.length() > 63) {
    return false;
  }

  command.type = CommandType::ConfigureWifi;
  return true;
}

size_t encodeStatus(const char* state, char* output, size_t outputSize) {
  JsonDocument document;
  document["type"] = "status";
  document["state"] = state;
  return serializeJson(document, output, outputSize);
}

size_t encodeError(const char* code, char* output, size_t outputSize) {
  JsonDocument document;
  document["type"] = "error";
  document["code"] = code;
  return serializeJson(document, output, outputSize);
}

size_t encodeScanResult(const GsmWifiAp* accessPoints,
                        size_t count,
                        char* output,
                        size_t outputSize) {
  JsonDocument document;
  document["type"] = "scan_result";
  JsonArray aps = document["aps"].to<JsonArray>();

  for (size_t index = 0; index < count; ++index) {
    JsonObject item = aps.add<JsonObject>();
    item["ssid"] = accessPoints[index].ssid;
    item["rssi"] = accessPoints[index].rssi;
    item["channel"] = accessPoints[index].channel;
  }

  return serializeJson(document, output, outputSize);
}

}  // namespace BleActivationProtocol
