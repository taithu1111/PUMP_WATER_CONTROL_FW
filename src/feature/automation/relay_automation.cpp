#include "relay_automation.h"

#include <cstring>
#include <utility>

#include "automation_storage.h"
#include "automation_time.h"
#include "app_config.h"
#include "log.h"

namespace RelayAutomation {
namespace {

constexpr uint32_t POLL_INTERVAL_MS = 250;

RelayContract::TimeoutConfig timeouts[AppConfig::System::OUTLET_COUNT];
RelayContract::ScheduleConfig schedules[AppConfig::System::OUTLET_COUNT];
RelayContract::OneShotScheduleConfig
    oneShotSchedules[AppConfig::System::OUTLET_COUNT];
bool timeoutRestorePending[AppConfig::System::OUTLET_COUNT]{};
uint64_t lastExecutedMinute[AppConfig::System::OUTLET_COUNT]
                           [RelayContract::MAX_SCHEDULE_EVENTS]{};
RelayWriter writeRelay;
ChangeHandler notifyChange;
bool started = false;
uint32_t lastPollMs = 0;

bool validChannel(uint8_t channel) {
  return channel >= 1 && channel <= AppConfig::System::OUTLET_COUNT;
}

uint8_t indexOf(uint8_t channel) {
  return channel - 1;
}

bool validSchedule(const RelayContract::ScheduleConfig& config) {
  if (config.eventCount > RelayContract::MAX_SCHEDULE_EVENTS ||
      (config.enabled && config.eventCount == 0)) return false;
  for (uint8_t i = 0; i < config.eventCount; ++i) {
    const auto& event = config.events[i];
    if (event.id == 0 || event.daysMask == 0 ||
        (event.daysMask & 0x80) != 0 || event.minuteOfDay > 1439) return false;
    for (uint8_t j = i + 1; j < config.eventCount; ++j) {
      if (event.id == config.events[j].id) return false;
    }
  }
  return true;
}

bool validOneShotSchedule(
    const RelayContract::OneShotScheduleConfig& config) {
  if (config.eventCount > RelayContract::MAX_SCHEDULE_EVENTS) return false;
  for (uint8_t i = 0; i < config.eventCount; ++i) {
    const auto& event = config.events[i];
    if (event.id == 0 || event.dueAt == 0 ||
        event.status > RelayContract::OneShotScheduleStatus::Executed)
      return false;
    for (uint8_t j = i + 1; j < config.eventCount; ++j) {
      if (event.id == config.events[j].id) return false;
    }
  }
  return true;
}

void clearOneShotSchedule(
    RelayContract::OneShotScheduleConfig& config) {
  config.enabled = false;
  config.eventCount = 0;
  for (uint8_t i = 0; i < RelayContract::MAX_SCHEDULE_EVENTS; ++i) {
    config.events[i].id = 0;
    config.events[i].dueAt = 0;
    config.events[i].state = false;
    config.events[i].status = RelayContract::OneShotScheduleStatus::Pending;
  }
}

void publishChange(uint8_t mask, uint8_t channel) {
  if (notifyChange) notifyChange(mask, channel);
}

Result cancelTimeoutInternal(uint8_t channel, bool notify) {
  const uint8_t index = indexOf(channel);
  if (!timeouts[index].active) return Result::Ok;
  if (!AutomationStorage::saveTimeout(channel, {})) return Result::StorageError;
  timeouts[index] = {};
  timeoutRestorePending[index] = false;
  if (notify) publishChange(RelayContract::TimeoutChanged, channel);
  return Result::Ok;
}

Result applyManual(uint8_t channel, bool state) {
  const Result cancelResult = cancelTimeoutInternal(channel, true);
  if (cancelResult != Result::Ok) return cancelResult;
  if (!writeRelay(channel, state)) return Result::RelayError;
  publishChange(RelayContract::RelayChanged, channel);
  return Result::Ok;
}

Result setTimeout(uint8_t channel, bool initialState, uint32_t durationSeconds,
                  bool expireState) {
  if (durationSeconds == 0 ||
      durationSeconds > RelayContract::MAX_TIMEOUT_SECONDS)
    return Result::InvalidArgument;
  const auto now = AutomationTime::snapshot();
  if (!now.valid || now.epochSeconds == 0) return Result::TimeUnavailable;

  const uint8_t index = indexOf(channel);
  const auto previous = timeouts[index];
  const bool previousPending = timeoutRestorePending[index];
  const RelayContract::TimeoutConfig next{
      true, initialState, expireState, now.epochSeconds + durationSeconds};
  if (!AutomationStorage::saveTimeout(channel, next)) return Result::StorageError;

  timeouts[index] = next;
  timeoutRestorePending[index] = false;
  if (!writeRelay(channel, initialState)) {
    AutomationStorage::saveTimeout(channel, previous);
    timeouts[index] = previous;
    timeoutRestorePending[index] = previousPending;
    return Result::RelayError;
  }
  publishChange(RelayContract::RelayChanged | RelayContract::TimeoutChanged,
                channel);
  return Result::Ok;
}

Result setSchedule(uint8_t channel,
                   const RelayContract::ScheduleConfig& config) {
  if (!validSchedule(config)) return Result::InvalidArgument;
  if (!AutomationStorage::saveSchedule(channel, config))
    return Result::StorageError;
  const uint8_t index = indexOf(channel);
  schedules[index] = config;
  std::memset(lastExecutedMinute[index], 0,
              sizeof(lastExecutedMinute[index]));
  publishChange(RelayContract::ScheduleChanged, channel);
  return Result::Ok;
}

Result saveOneShotSchedule(
    uint8_t channel,
    const RelayContract::OneShotScheduleConfig& next) {
  if (!validOneShotSchedule(next)) return Result::InvalidArgument;
  if (!AutomationStorage::saveOneShotSchedule(channel, next))
    return Result::StorageError;
  oneShotSchedules[indexOf(channel)] = next;
  publishChange(RelayContract::ScheduleChanged, channel);
  return Result::Ok;
}

Result upsertOneShotSchedule(
    uint8_t channel, const RelayContract::OneShotScheduleEvent& commandEvent) {
  if (commandEvent.id == 0 || commandEvent.dueAt == 0)
    return Result::InvalidArgument;

  auto next = oneShotSchedules[indexOf(channel)];
  uint8_t eventIndex = next.eventCount;
  for (uint8_t i = 0; i < next.eventCount; ++i) {
    if (next.events[i].id == commandEvent.id) {
      eventIndex = i;
      break;
    }
  }
  if (eventIndex == next.eventCount) {
    if (next.eventCount >= RelayContract::MAX_SCHEDULE_EVENTS)
      return Result::InvalidArgument;
    ++next.eventCount;
  }
  next.events[eventIndex] = commandEvent;
  next.events[eventIndex].status =
      RelayContract::OneShotScheduleStatus::Pending;
  return saveOneShotSchedule(channel, next);
}

Result deleteOneShotSchedule(uint8_t channel, uint8_t eventId) {
  if (eventId == 0) return Result::InvalidArgument;
  auto next = oneShotSchedules[indexOf(channel)];
  uint8_t eventIndex = next.eventCount;
  for (uint8_t i = 0; i < next.eventCount; ++i) {
    if (next.events[i].id == eventId) {
      eventIndex = i;
      break;
    }
  }
  if (eventIndex == next.eventCount) return Result::InvalidArgument;
  for (uint8_t i = eventIndex; i + 1 < next.eventCount; ++i) {
    next.events[i] = next.events[i + 1];
  }
  --next.eventCount;
  next.events[next.eventCount].id = 0;
  next.events[next.eventCount].dueAt = 0;
  next.events[next.eventCount].state = false;
  next.events[next.eventCount].status =
      RelayContract::OneShotScheduleStatus::Pending;
  return saveOneShotSchedule(channel, next);
}

Result setOneShotScheduleEnabled(uint8_t channel, bool enabled) {
  auto next = oneShotSchedules[indexOf(channel)];
  next.enabled = enabled;
  return saveOneShotSchedule(channel, next);
}

void processTimeout(uint8_t channel,
                    const RelayContract::TimeSnapshot& now) {
  const uint8_t index = indexOf(channel);
  auto& timeout = timeouts[index];
  if (!timeout.active) return;

  if (now.epochSeconds >= timeout.expiresAt) {
    if (!writeRelay(channel, timeout.expireState)) return;
    if (!AutomationStorage::saveTimeout(channel, {})) return;
    timeout = {};
    timeoutRestorePending[index] = false;
    publishChange(RelayContract::RelayChanged | RelayContract::TimeoutChanged,
                  channel);
    return;
  }

  if (timeoutRestorePending[index] &&
      writeRelay(channel, timeout.initialState)) {
    timeoutRestorePending[index] = false;
    publishChange(RelayContract::RelayChanged, channel);
  }
}

void processSchedule(uint8_t channel,
                     const RelayContract::TimeSnapshot& now) {
  const uint8_t index = indexOf(channel);
  const auto& schedule = schedules[index];
  if (!schedule.enabled || timeouts[index].active) return;
  const uint8_t todayBit = static_cast<uint8_t>(1U << (now.weekday - 1));
  const uint64_t epochMinute = now.epochSeconds / 60;
  for (uint8_t i = 0; i < schedule.eventCount; ++i) {
    const auto& event = schedule.events[i];
    if ((event.daysMask & todayBit) == 0 ||
        event.minuteOfDay != now.minuteOfDay ||
        lastExecutedMinute[index][i] == epochMinute) continue;
    if (!writeRelay(channel, event.state)) continue;
    lastExecutedMinute[index][i] = epochMinute;
    publishChange(RelayContract::RelayChanged, channel);
  }
}

void processOneShotSchedule(uint8_t channel,
                            const RelayContract::TimeSnapshot& now) {
  const uint8_t index = indexOf(channel);
  auto& schedule = oneShotSchedules[index];
  if (!schedule.enabled || timeouts[index].active) return;

  for (uint8_t i = 0; i < schedule.eventCount; ++i) {
    auto& event = schedule.events[i];
    if (event.status != RelayContract::OneShotScheduleStatus::Pending ||
        event.dueAt > now.epochSeconds)
      continue;
    if (!writeRelay(channel, event.state)) continue;

    event.status = RelayContract::OneShotScheduleStatus::Executed;
    if (!AutomationStorage::saveOneShotSchedule(channel, schedule)) {
      event.status = RelayContract::OneShotScheduleStatus::Pending;
      continue;
    }
    publishChange(RelayContract::RelayChanged |
                      RelayContract::ScheduleChanged,
                  channel);
  }
}

}  // namespace

bool begin(RelayWriter relayWriter, ChangeHandler changeHandler) {
  if (!relayWriter) return false;
  AutomationTime::begin();
  if (!AutomationStorage::begin()) return false;
  writeRelay = std::move(relayWriter);
  notifyChange = std::move(changeHandler);
  for (uint8_t channel = 1; channel <= AppConfig::System::OUTLET_COUNT;
       ++channel) {
    const uint8_t index = indexOf(channel);
    timeoutRestorePending[index] =
        AutomationStorage::loadTimeout(channel, timeouts[index]);
    if (!AutomationStorage::loadSchedule(channel, schedules[index]) ||
        !validSchedule(schedules[index])) {
      schedules[index] = {};
    }
    if (!AutomationStorage::loadOneShotSchedule(
            channel, oneShotSchedules[index]) ||
        !validOneShotSchedule(oneShotSchedules[index])) {
      clearOneShotSchedule(oneShotSchedules[index]);
    }
  }
  started = true;
  return true;
}

void end() {
  if (started) AutomationStorage::end();
  started = false;
  writeRelay = nullptr;
  notifyChange = nullptr;
}

Result handleCommand(const RelayContract::Command& command) {
  if (!started) return Result::NotStarted;
  if (!validChannel(command.channel)) return Result::InvalidChannel;
  Result result = Result::InvalidArgument;
  switch (command.type) {
    case RelayContract::CommandType::SetChannel:
      result = applyManual(command.channel, command.state);
      break;
    case RelayContract::CommandType::SetTimeout:
      result = setTimeout(command.channel, command.state,
                          command.durationSeconds, command.expireState);
      break;
    case RelayContract::CommandType::CancelTimeout:
      result = cancelTimeoutInternal(command.channel, true);
      break;
    case RelayContract::CommandType::SetSchedule:
      result = setSchedule(command.channel, command.schedule);
      break;
    case RelayContract::CommandType::UpsertOneShotSchedule:
      result = upsertOneShotSchedule(command.channel, command.oneShotEvent);
      break;
    case RelayContract::CommandType::DeleteOneShotSchedule:
      result = deleteOneShotSchedule(command.channel,
                                     command.scheduleEventId);
      break;
    case RelayContract::CommandType::SetOneShotScheduleEnabled:
      result = setOneShotScheduleEnabled(command.channel,
                                         command.scheduleEnabled);
      break;
  }
  if (result != Result::Ok)
    LOG_PRINTF("[automation] Command failed: %s\n", resultName(result));
  return result;
}

bool getSnapshot(RelayContract::AutomationSnapshot& output) {
  if (!started) return false;
  for (uint8_t i = 0; i < AppConfig::System::OUTLET_COUNT; ++i) {
    output.timeouts[i] = timeouts[i];
    output.schedules[i] = schedules[i];
    output.oneShotSchedules[i] = oneShotSchedules[i];
  }
  return true;
}

void loop() {
  if (!started) return;
  AutomationTime::loop();
  const uint32_t nowMs = millis();
  if (static_cast<uint32_t>(nowMs - lastPollMs) < POLL_INTERVAL_MS) return;
  lastPollMs = nowMs;
  const auto now = AutomationTime::snapshot();
  if (!now.valid || now.epochSeconds == 0 || now.weekday < 1 ||
      now.weekday > 7 || now.minuteOfDay > 1439) return;
  for (uint8_t channel = 1; channel <= AppConfig::System::OUTLET_COUNT;
       ++channel) {
    processTimeout(channel, now);
    processSchedule(channel, now);
    processOneShotSchedule(channel, now);
  }
}

const char* resultName(Result result) {
  switch (result) {
    case Result::Ok: return "ok";
    case Result::InvalidChannel: return "invalid_channel";
    case Result::InvalidArgument: return "invalid_argument";
    case Result::TimeUnavailable: return "time_unavailable";
    case Result::StorageError: return "storage_error";
    case Result::RelayError: return "relay_error";
    case Result::NotStarted: return "not_started";
  }
  return "unknown";
}

}  // namespace RelayAutomation
