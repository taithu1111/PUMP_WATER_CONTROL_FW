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
  UpsertIntervalSchedule,
  DeleteIntervalSchedule,
  SetIntervalScheduleEnabled,
};

enum class CommandResult : uint8_t {
  Ok,
  InvalidChannel,
  InvalidArgument,
  TimeUnavailable,
  StorageError,
  RelayError,
  NotStarted,
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

enum class IntervalScheduleStatus : uint8_t {
  Pending,
  Active,
  Completed,
  Missed,
};

// An interval schedule turns its relay ON at startAt and OFF at endAt.
// Both timestamps are absolute UTC epochs parsed from MQTT +07:00 values.
struct IntervalScheduleEntry {
  uint8_t id = 0;
  uint64_t startAt = 0;
  uint64_t endAt = 0;
  IntervalScheduleStatus status = IntervalScheduleStatus::Pending;
};

struct IntervalScheduleConfig {
  bool enabled = false;
  uint8_t entryCount = 0;
  IntervalScheduleEntry entries[MAX_SCHEDULE_EVENTS]{};
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
  IntervalScheduleEntry intervalSchedule{};
  uint8_t intervalScheduleId = 0;
  bool intervalScheduleEnabled = false;
};

struct AutomationSnapshot {
  uint64_t currentEpoch = 0;
  TimeoutConfig timeouts[AppConfig::System::OUTLET_COUNT]{};
  ScheduleConfig schedules[AppConfig::System::OUTLET_COUNT]{};
  OneShotScheduleConfig oneShotSchedules[AppConfig::System::OUTLET_COUNT]{};
  IntervalScheduleConfig
      intervalSchedules[AppConfig::System::OUTLET_COUNT]{};
};

struct TimeSnapshot {
  bool valid = false;
  uint64_t epochSeconds = 0;
  uint8_t weekday = 0;
  uint16_t minuteOfDay = 0;
};

}  // namespace RelayContract
