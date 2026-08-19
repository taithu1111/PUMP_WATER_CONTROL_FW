#pragma once

#include "relay_contract.h"

namespace AutomationTime {

bool begin();
void loop();
RelayContract::TimeSnapshot snapshot();

}  // namespace AutomationTime
