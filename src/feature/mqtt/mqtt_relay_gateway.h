#pragma once

#include <functional>

#include "relay_contract.h"

namespace MqttRelayGateway {

using CommandHandler =
    std::function<RelayContract::CommandResult(const RelayContract::Command&)>;
using AutomationSnapshotProvider =
    std::function<bool(RelayContract::AutomationSnapshot&)>;
using RelayStateProvider = std::function<void(bool*)>;

bool begin(const char* ssid, const char* password,
           CommandHandler commandHandler,
           AutomationSnapshotProvider automationProvider,
           RelayStateProvider relayProvider);
void loop();
void stop();
bool isConnected();

void notifyStateChange(uint8_t changeMask, uint8_t channel);

}  // namespace MqttRelayGateway
