#pragma once

#include "relay_contract.h"

namespace AutomationStorage {

bool begin();
void end();
bool loadTimeout(uint8_t channel, RelayContract::TimeoutConfig& output);
bool saveTimeout(uint8_t channel, const RelayContract::TimeoutConfig& config);
bool loadSchedule(uint8_t channel, RelayContract::ScheduleConfig& output);
bool saveSchedule(uint8_t channel, const RelayContract::ScheduleConfig& config);
bool loadOneShotSchedule(uint8_t channel,
                         RelayContract::OneShotScheduleConfig& output);
bool saveOneShotSchedule(
    uint8_t channel, const RelayContract::OneShotScheduleConfig& config);

}  // namespace AutomationStorage
