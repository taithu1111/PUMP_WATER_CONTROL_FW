#pragma once

#include "relay_contract.h"

namespace AutomationStorage {

bool begin();
void end();
bool loadTimeout(uint8_t channel, RelayContract::TimeoutConfig& output);
bool saveTimeout(uint8_t channel, const RelayContract::TimeoutConfig& config);
bool loadIntervalSchedule(uint8_t channel,
                          RelayContract::IntervalScheduleConfig& output);
bool saveIntervalSchedule(
    uint8_t channel, const RelayContract::IntervalScheduleConfig& config);

}  // namespace AutomationStorage
