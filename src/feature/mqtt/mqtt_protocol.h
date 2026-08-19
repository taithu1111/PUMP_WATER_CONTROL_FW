#pragma once

#include <Arduino.h>

#include "app_config.h"
#include "relay_contract.h"

namespace MqttProtocol {

constexpr size_t TOPIC_SIZE = 96;

struct Topics {
  char set[TOPIC_SIZE];
  char get[TOPIC_SIZE];
  char state[TOPIC_SIZE];
  char timeoutSet[TOPIC_SIZE];
  char timeoutGet[TOPIC_SIZE];
  char timeoutState[TOPIC_SIZE];
  char scheduleSet[TOPIC_SIZE];
  char scheduleGet[TOPIC_SIZE];
  char scheduleState[TOPIC_SIZE];
  char status[TOPIC_SIZE];
};

void buildTopics(Topics& topics);
bool decodeSetCommand(const uint8_t* payload, size_t length,
                      RelayContract::Command& command);
bool decodeTimeoutCommand(const uint8_t* payload, size_t length,
                          RelayContract::Command& command);
bool decodeScheduleCommand(const uint8_t* payload, size_t length,
                           RelayContract::Command& command);
bool decodeOneShotScheduleCommand(const uint8_t* payload, size_t length,
                                  RelayContract::Command& command);
size_t encodeRelayStates(
    const bool states[AppConfig::System::OUTLET_COUNT], char* output,
    size_t outputSize);
size_t encodeTimeoutStates(const RelayContract::AutomationSnapshot& snapshot,
                           char* output, size_t outputSize);
size_t encodeScheduleStates(const RelayContract::AutomationSnapshot& snapshot,
                            char* output, size_t outputSize);
size_t encodeOneShotScheduleStates(
    const RelayContract::AutomationSnapshot& snapshot, uint64_t nowEpoch,
    char* output, size_t outputSize);

}  // namespace MqttProtocol
