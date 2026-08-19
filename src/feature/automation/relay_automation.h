#pragma once

#include <functional>

#include "relay_contract.h"

namespace RelayAutomation {

using Result = RelayContract::CommandResult;

using RelayWriter = std::function<bool(uint8_t channel, bool state)>;
using ChangeHandler = std::function<void(uint8_t changeMask, uint8_t channel)>;

bool begin(RelayWriter relayWriter, ChangeHandler changeHandler = nullptr);
void end();
void loop();

Result handleCommand(const RelayContract::Command& command);
bool getSnapshot(RelayContract::AutomationSnapshot& output);
const char* resultName(Result result);

}  // namespace RelayAutomation
