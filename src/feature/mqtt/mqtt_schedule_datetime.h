#pragma once

#include <Arduino.h>

namespace MqttScheduleDateTime {

bool parse(const char* day, const char* dueDate, uint64_t& dueAt);
void format(uint64_t dueAt, char day[11], char dueDate[26]);

}  // namespace MqttScheduleDateTime
