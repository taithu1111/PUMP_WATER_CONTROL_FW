#pragma once

#include <Arduino.h>

namespace MqttScheduleDateTime {

bool parseTimestamp(const char* timestamp, uint64_t& epoch);
void formatTimestamp(uint64_t epoch, char timestamp[26]);

}  // namespace MqttScheduleDateTime
