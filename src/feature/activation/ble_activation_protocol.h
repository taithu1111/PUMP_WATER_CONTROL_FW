#pragma once

#include <Arduino.h>

#include "gsmlink.h"

namespace BleActivationProtocol {

enum class CommandType : uint8_t {
  Scan,
  ConfigureWifi,
};

struct Command {
  CommandType type;
  String ssid;
  String password;
};

bool decodeCommand(const uint8_t* data, size_t length, Command& command);
size_t encodeStatus(const char* state, char* output, size_t outputSize);
size_t encodeError(const char* code, char* output, size_t outputSize);
size_t encodeScanResult(const GsmWifiAp* accessPoints,
                        size_t count,
                        char* output,
                        size_t outputSize);

}  // namespace BleActivationProtocol
