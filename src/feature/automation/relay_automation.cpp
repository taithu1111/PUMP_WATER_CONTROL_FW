#include "relay_automation.h"

#include <utility>

#include "automation_storage.h"
#include "automation_time.h"
#include "app_config.h"
#include "log.h"

namespace RelayAutomation {
namespace {

constexpr uint32_t POLL_INTERVAL_MS = 250;

RelayContract::TimeoutConfig timeouts[AppConfig::System::OUTLET_COUNT];
RelayContract::IntervalScheduleConfig
    intervalSchedules[AppConfig::System::OUTLET_COUNT];
bool timeoutRestorePending[AppConfig::System::OUTLET_COUNT]{};
bool intervalResumePending[AppConfig::System::OUTLET_COUNT]{};
RelayWriter writeRelay;
ChangeHandler notifyChange;
bool started = false;
bool automationPaused = true;
uint32_t lastPollMs = 0;

bool validChannel(uint8_t channel) {
  return channel >= 1 && channel <= AppConfig::System::OUTLET_COUNT;
}

uint8_t indexOf(uint8_t channel) {
  return channel - 1;
}

bool validIntervalSchedule(
    const RelayContract::IntervalScheduleConfig& config) {
  if (config.entryCount > RelayContract::MAX_SCHEDULE_EVENTS) return false;
  for (uint8_t i = 0; i < config.entryCount; ++i) {
    const auto& entry = config.entries[i];
    if (entry.id == 0 || entry.startAt == 0 || entry.endAt == 0 ||
        entry.startAt >= entry.endAt ||
        entry.status > RelayContract::IntervalScheduleStatus::Missed)
      return false;
    for (uint8_t j = i + 1; j < config.entryCount; ++j) {
      const auto& other = config.entries[j];
      if (entry.id == other.id ||
          (entry.startAt < other.endAt && other.startAt < entry.endAt))
        return false;
    }
  }
  return true;
}

void clearIntervalSchedule(
    RelayContract::IntervalScheduleConfig& config) {
  config.enabled = false;
  config.entryCount = 0;
  for (uint8_t i = 0; i < RelayContract::MAX_SCHEDULE_EVENTS; ++i) {
    config.entries[i].id = 0;
    config.entries[i].startAt = 0;
    config.entries[i].endAt = 0;
    config.entries[i].status = RelayContract::IntervalScheduleStatus::Pending;
  }
}

void sortIntervalEntries(RelayContract::IntervalScheduleConfig& config) {
  for (uint8_t i = 1; i < config.entryCount; ++i) {
    auto entry = config.entries[i];
    uint8_t position = i;
    while (position > 0 &&
           config.entries[position - 1].startAt > entry.startAt) {
      config.entries[position] = config.entries[position - 1];
      --position;
    }
    config.entries[position] = entry;
  }
}

bool hasActiveInterval(uint8_t index) {
  const auto& config = intervalSchedules[index];
  for (uint8_t i = 0; i < config.entryCount; ++i) {
    if (config.entries[i].status ==
        RelayContract::IntervalScheduleStatus::Active)
      return true;
  }
  return false;
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
  intervalResumePending[indexOf(channel)] = false;
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
  if (hasActiveInterval(index)) intervalResumePending[index] = true;
  publishChange(RelayContract::RelayChanged | RelayContract::TimeoutChanged,
                channel);
  return Result::Ok;
}

Result saveIntervalSchedule(
    uint8_t channel, RelayContract::IntervalScheduleConfig next) {
  sortIntervalEntries(next);
  if (!validIntervalSchedule(next)) return Result::InvalidArgument;
  if (!AutomationStorage::saveIntervalSchedule(channel, next))
    return Result::StorageError;
  intervalSchedules[indexOf(channel)] = next;
  publishChange(RelayContract::ScheduleChanged, channel);
  return Result::Ok;
}

Result upsertIntervalSchedule(
    uint8_t channel, const RelayContract::IntervalScheduleEntry& commandEntry) {
  if (commandEntry.id == 0 || commandEntry.startAt == 0 ||
      commandEntry.endAt == 0 || commandEntry.startAt >= commandEntry.endAt)
    return Result::InvalidArgument;

  auto next = intervalSchedules[indexOf(channel)];
  uint8_t entryIndex = next.entryCount;
  for (uint8_t i = 0; i < next.entryCount; ++i) {
    if (next.entries[i].id == commandEntry.id) {
      if (next.entries[i].status ==
          RelayContract::IntervalScheduleStatus::Active)
        return Result::InvalidArgument;
      entryIndex = i;
      break;
    }
  }
  if (entryIndex == next.entryCount) {
    if (next.entryCount >= RelayContract::MAX_SCHEDULE_EVENTS)
      return Result::InvalidArgument;
    ++next.entryCount;
  }
  next.entries[entryIndex] = commandEntry;
  next.entries[entryIndex].status =
      RelayContract::IntervalScheduleStatus::Pending;
  return saveIntervalSchedule(channel, next);
}

Result deleteIntervalSchedule(uint8_t channel, uint8_t scheduleId) {
  if (scheduleId == 0) return Result::InvalidArgument;
  const uint8_t index = indexOf(channel);
  auto next = intervalSchedules[index];
  uint8_t entryIndex = next.entryCount;
  for (uint8_t i = 0; i < next.entryCount; ++i) {
    if (next.entries[i].id == scheduleId) {
      entryIndex = i;
      break;
    }
  }
  if (entryIndex == next.entryCount) return Result::InvalidArgument;

  const bool wasActive = next.entries[entryIndex].status ==
                         RelayContract::IntervalScheduleStatus::Active;
  const bool relayWasStopped =
      wasActive && !timeouts[index].active && writeRelay(channel, false);
  if (wasActive && !timeouts[index].active && !relayWasStopped)
    return Result::RelayError;

  for (uint8_t i = entryIndex; i + 1 < next.entryCount; ++i) {
    next.entries[i] = next.entries[i + 1];
  }
  --next.entryCount;
  next.entries[next.entryCount].id = 0;
  next.entries[next.entryCount].startAt = 0;
  next.entries[next.entryCount].endAt = 0;
  next.entries[next.entryCount].status =
      RelayContract::IntervalScheduleStatus::Pending;
  const Result result = saveIntervalSchedule(channel, next);
  if (result != Result::Ok && relayWasStopped)
    intervalResumePending[index] = true;
  if (result == Result::Ok && relayWasStopped)
    publishChange(RelayContract::RelayChanged, channel);
  return result;
}

Result setIntervalScheduleEnabled(uint8_t channel, bool enabled) {
  auto next = intervalSchedules[indexOf(channel)];
  next.enabled = enabled;
  return saveIntervalSchedule(channel, next);
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

void processIntervalSchedule(uint8_t channel,
                             const RelayContract::TimeSnapshot& now) {
  const uint8_t index = indexOf(channel);
  auto& config = intervalSchedules[index];

  for (uint8_t i = 0; i < config.entryCount; ++i) {
    auto& entry = config.entries[i];
    if (entry.status == RelayContract::IntervalScheduleStatus::Active &&
        now.epochSeconds >= entry.endAt) {
      if (timeouts[index].active) continue;
      if (!writeRelay(channel, false)) continue;
      entry.status = RelayContract::IntervalScheduleStatus::Completed;
      if (!AutomationStorage::saveIntervalSchedule(channel, config)) {
        entry.status = RelayContract::IntervalScheduleStatus::Active;
        intervalResumePending[index] = true;
        continue;
      }
      intervalResumePending[index] = false;
      publishChange(RelayContract::RelayChanged |
                        RelayContract::ScheduleChanged,
                    channel);
    }
  }

  for (uint8_t i = 0; i < config.entryCount; ++i) {
    auto& entry = config.entries[i];
    if (entry.status != RelayContract::IntervalScheduleStatus::Pending)
      continue;
    if (now.epochSeconds >= entry.endAt) {
      entry.status = RelayContract::IntervalScheduleStatus::Missed;
      if (!AutomationStorage::saveIntervalSchedule(channel, config)) {
        entry.status = RelayContract::IntervalScheduleStatus::Pending;
        continue;
      }
      publishChange(RelayContract::ScheduleChanged, channel);
      continue;
    }
    if (!config.enabled || timeouts[index].active ||
        now.epochSeconds < entry.startAt)
      continue;
    if (!writeRelay(channel, true)) continue;
    entry.status = RelayContract::IntervalScheduleStatus::Active;
    if (!AutomationStorage::saveIntervalSchedule(channel, config)) {
      entry.status = RelayContract::IntervalScheduleStatus::Pending;
      continue;
    }
    intervalResumePending[index] = false;
    publishChange(RelayContract::RelayChanged |
                      RelayContract::ScheduleChanged,
                  channel);
  }

  if (!timeouts[index].active && intervalResumePending[index] &&
      hasActiveInterval(index) && writeRelay(channel, true)) {
    intervalResumePending[index] = false;
    publishChange(RelayContract::RelayChanged, channel);
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
    if (!AutomationStorage::loadIntervalSchedule(
            channel, intervalSchedules[index]) ||
        !validIntervalSchedule(intervalSchedules[index])) {
      clearIntervalSchedule(intervalSchedules[index]);
    }
    intervalResumePending[index] = hasActiveInterval(index);
  }
  automationPaused = true;
  started = true;
  return true;
}

void end() {
  if (started) AutomationStorage::end();
  started = false;
  automationPaused = true;
  writeRelay = nullptr;
  notifyChange = nullptr;
}

void setPaused(bool paused) {
  if (!started || automationPaused == paused) return;

  automationPaused = paused;
  if (!automationPaused) {
    for (uint8_t index = 0; index < AppConfig::System::OUTLET_COUNT; ++index) {
      if (timeouts[index].active) timeoutRestorePending[index] = true;
      if (hasActiveInterval(index)) intervalResumePending[index] = true;
    }
  }
  LOG_PRINTF("[automation] %s\n", automationPaused ? "Paused" : "Resumed");
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
    case RelayContract::CommandType::UpsertIntervalSchedule:
      result = upsertIntervalSchedule(command.channel,
                                      command.intervalSchedule);
      break;
    case RelayContract::CommandType::DeleteIntervalSchedule:
      result = deleteIntervalSchedule(command.channel,
                                      command.intervalScheduleId);
      break;
    case RelayContract::CommandType::SetIntervalScheduleEnabled:
      result = setIntervalScheduleEnabled(
          command.channel, command.intervalScheduleEnabled);
      break;
  }
  if (result != Result::Ok)
    LOG_PRINTF("[automation] Command failed: %s\n", resultName(result));
  return result;
}

bool getSnapshot(RelayContract::AutomationSnapshot& output) {
  if (!started) return false;
  const auto now = AutomationTime::snapshot();
  output.currentEpoch = now.valid ? now.epochSeconds : 0;
  for (uint8_t i = 0; i < AppConfig::System::OUTLET_COUNT; ++i) {
    output.timeouts[i] = timeouts[i];
    output.intervalSchedules[i] = intervalSchedules[i];
  }
  return true;
}

void loop() {
  if (!started) return;
  AutomationTime::loop();
  if (automationPaused) return;
  const uint32_t nowMs = millis();
  if (static_cast<uint32_t>(nowMs - lastPollMs) < POLL_INTERVAL_MS) return;
  lastPollMs = nowMs;
  const auto now = AutomationTime::snapshot();
  if (!now.valid || now.epochSeconds == 0) return;
  for (uint8_t channel = 1; channel <= AppConfig::System::OUTLET_COUNT;
       ++channel) {
    processTimeout(channel, now);
    processIntervalSchedule(channel, now);
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
