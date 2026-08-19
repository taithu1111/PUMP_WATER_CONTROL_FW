#pragma once

#include <Arduino.h>

#include "app_config.h"

namespace RelayContract {

constexpr uint8_t MAX_SCHEDULE_EVENTS = 8;
constexpr uint32_t MAX_TIMEOUT_SECONDS = 30UL * 24UL * 60UL * 60UL;

enum class CommandType : uint8_t {
  SetChannel,
  SetTimeout,
  CancelTimeout,
  SetSchedule,
  UpsertOneShotSchedule,
  DeleteOneShotSchedule,
  SetOneShotScheduleEnabled,
};

enum ChangeMask : uint8_t {
  RelayChanged = 1U << 0,
  TimeoutChanged = 1U << 1,
  ScheduleChanged = 1U << 2,
};

enum class OneShotScheduleStatus : uint8_t {
  Pending,
  Executed,
};

// dueAt stores the absolute UTC epoch parsed from the day/dueDate MQTT fields.
struct OneShotScheduleEvent {
  uint8_t id = 0;
  uint64_t dueAt = 0;
  bool state = false;
  OneShotScheduleStatus status = OneShotScheduleStatus::Pending;
};

struct OneShotScheduleConfig {
  bool enabled = false;
  uint8_t eventCount = 0;
  OneShotScheduleEvent events[MAX_SCHEDULE_EVENTS]{};
};

struct ScheduleEvent {
  uint8_t id = 0;
  uint8_t daysMask = 0;
  uint16_t minuteOfDay = 0;
  bool state = false;
};

struct ScheduleConfig {
  bool enabled = false;
  uint8_t eventCount = 0;
  ScheduleEvent events[MAX_SCHEDULE_EVENTS]{};
};

struct TimeoutConfig {
  bool active = false;
  bool initialState = false;
  bool expireState = false;
  uint64_t expiresAt = 0;
};

struct Command {
  CommandType type{};
  uint8_t channel = 0;
  bool state = false;
  uint32_t durationSeconds = 0;
  bool expireState = false;
  ScheduleConfig schedule{};
  OneShotScheduleEvent oneShotEvent{};
  uint8_t scheduleEventId = 0;
  bool scheduleEnabled = false;
};

struct AutomationSnapshot {
  uint64_t currentEpoch = 0;
  TimeoutConfig timeouts[AppConfig::System::OUTLET_COUNT]{};
  ScheduleConfig schedules[AppConfig::System::OUTLET_COUNT]{};
  OneShotScheduleConfig oneShotSchedules[AppConfig::System::OUTLET_COUNT]{};
};

struct TimeSnapshot {
  bool valid = false;
  uint64_t epochSeconds = 0;
  uint8_t weekday = 0;
  uint16_t minuteOfDay = 0;
};

}  // namespace RelayContract
