#include "relay_feature.h"

#include <Wire.h>

#include "app_config.h"
#include "log.h"

namespace RelayFeature {
namespace {

uint16_t outputState = 0xFFFF;
bool channelStates[AppConfig::System::OUTLET_COUNT] = {};
bool ready = false;

bool writeOutputs(uint16_t state) {
  Wire.beginTransmission(AppConfig::Hardware::PCF8575_ADDRESS);
  Wire.write(static_cast<uint8_t>(state & 0xFF));
  Wire.write(static_cast<uint8_t>((state >> 8) & 0xFF));
  return Wire.endTransmission() == 0;
}

bool isValidChannel(uint8_t channel) {
  return channel >= 1 && channel <= AppConfig::System::OUTLET_COUNT;
}

uint16_t stateWithChannel(uint16_t state, uint8_t channel, bool enabled) {
  const uint8_t port = AppConfig::Hardware::RELAY_PCF_PORTS[channel - 1];
  const bool outputHigh = AppConfig::Hardware::RELAY_ACTIVE_LOW ? !enabled : enabled;

  if (outputHigh) {
    return state | (static_cast<uint16_t>(1) << port);
  }
  return state & ~(static_cast<uint16_t>(1) << port);
}

}  // namespace

bool begin() {
  ready = false;
  outputState = 0xFFFF;

  for (uint8_t index = 0; index < AppConfig::System::OUTLET_COUNT; ++index) {
    channelStates[index] = false;
  }

  if (!Wire.begin(AppConfig::Hardware::I2C_SDA_PIN,
                  AppConfig::Hardware::I2C_SCL_PIN,
                  AppConfig::Hardware::I2C_FREQUENCY_HZ)) {
    LOG_PRINTLN("[relay] I2C initialization failed");
    return false;
  }

  if (!writeOutputs(outputState)) {
    LOG_PRINTLN("[relay] PCF8575 not responding");
    return false;
  }

  ready = true;
  LOG_PRINTLN("[relay] Four channels initialized OFF");
  return true;
}

bool setChannel(uint8_t channel, bool enabled) {
  if (!ready || !isValidChannel(channel)) {
    return false;
  }

  const uint16_t nextState = stateWithChannel(outputState, channel, enabled);
  if (!writeOutputs(nextState)) {
    LOG_PRINTF("[relay] Failed to write channel %u\n", channel);
    return false;
  }

  outputState = nextState;
  channelStates[channel - 1] = enabled;
  return true;
}

bool getChannel(uint8_t channel) {
  if (!isValidChannel(channel)) {
    return false;
  }
  return channelStates[channel - 1];
}

void getAll(bool states[AppConfig::System::OUTLET_COUNT]) {
  if (states == nullptr) {
    return;
  }

  for (uint8_t index = 0; index < AppConfig::System::OUTLET_COUNT; ++index) {
    states[index] = channelStates[index];
  }
}

bool allOff() {
  if (!ready) {
    return false;
  }

  uint16_t nextState = outputState;
  for (uint8_t channel = 1; channel <= AppConfig::System::OUTLET_COUNT; ++channel) {
    nextState = stateWithChannel(nextState, channel, false);
  }

  if (!writeOutputs(nextState)) {
    LOG_PRINTLN("[relay] Failed to switch all channels OFF");
    return false;
  }

  outputState = nextState;
  for (uint8_t index = 0; index < AppConfig::System::OUTLET_COUNT; ++index) {
    channelStates[index] = false;
  }
  return true;
}

bool isReady() {
  return ready;
}

}  // namespace RelayFeature
