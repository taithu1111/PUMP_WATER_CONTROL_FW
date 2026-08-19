#pragma once

#include <Arduino.h>

#include "app_config.h"

namespace RelayFeature {

// Khởi tạo PCF8575 và đưa toàn bộ relay về trạng thái tắt an toàn.
bool begin();

// Kênh relay được đánh số từ 1 đến OUTLET_COUNT.
bool setChannel(uint8_t channel, bool enabled);
bool getChannel(uint8_t channel);
void getAll(bool states[AppConfig::System::OUTLET_COUNT]);
bool allOff();
bool isReady();

}  // namespace RelayFeature
