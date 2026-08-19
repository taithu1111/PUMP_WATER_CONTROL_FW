#include "automation_storage.h"

#include <Preferences.h>

namespace AutomationStorage {
namespace {

constexpr char NVS_NAMESPACE[] = "relay_auto";
constexpr uint32_t STORAGE_MAGIC = 0x52415554;
constexpr uint16_t STORAGE_VERSION = 1;
constexpr uint16_t INTERVAL_STORAGE_VERSION = 3;

struct StoredTimeout {
  uint32_t magic;
  uint16_t version;
  uint8_t active;
  uint8_t initialState;
  uint8_t expireState;
  uint8_t reserved[3];
  uint64_t expiresAt;
};

struct StoredIntervalScheduleEntry {
  uint8_t id;
  uint8_t status;
  uint8_t reserved[6];
  uint64_t startAt;
  uint64_t endAt;
};

struct StoredIntervalSchedule {
  uint32_t magic;
  uint16_t version;
  uint8_t enabled;
  uint8_t entryCount;
  StoredIntervalScheduleEntry entries[RelayContract::MAX_SCHEDULE_EVENTS];
};

Preferences preferences;

void makeKey(char prefix, uint8_t channel, char key[3]) {
  key[0] = prefix;
  key[1] = static_cast<char>('0' + channel);
  key[2] = '\0';
}

bool isValidChannel(uint8_t channel) {
  return channel >= 1 && channel <= AppConfig::System::OUTLET_COUNT;
}

bool isValidIntervalStatus(uint8_t status) {
  return status <=
         static_cast<uint8_t>(RelayContract::IntervalScheduleStatus::Missed);
}

bool hasValidIntervalEntries(
    const RelayContract::IntervalScheduleConfig& config) {
  if (config.entryCount > RelayContract::MAX_SCHEDULE_EVENTS) return false;
  for (uint8_t i = 0; i < config.entryCount; ++i) {
    const auto& entry = config.entries[i];
    if (entry.id == 0 || entry.startAt == 0 || entry.endAt == 0 ||
        entry.startAt >= entry.endAt ||
        !isValidIntervalStatus(static_cast<uint8_t>(entry.status)))
      return false;
    for (uint8_t j = 0; j < i; ++j) {
      if (config.entries[j].id == entry.id) return false;
    }
  }
  return true;
}

void resetIntervalSchedule(RelayContract::IntervalScheduleConfig& config) {
  config.enabled = false;
  config.entryCount = 0;
  for (uint8_t i = 0; i < RelayContract::MAX_SCHEDULE_EVENTS; ++i) {
    config.entries[i].id = 0;
    config.entries[i].startAt = 0;
    config.entries[i].endAt = 0;
    config.entries[i].status = RelayContract::IntervalScheduleStatus::Pending;
  }
}

}  // namespace

bool begin() {
  return preferences.begin(NVS_NAMESPACE, false);
}

void end() {
  preferences.end();
}

bool loadTimeout(uint8_t channel, RelayContract::TimeoutConfig& output) {
  char key[3];
  makeKey('t', channel, key);
  StoredTimeout stored{};
  if (preferences.getBytesLength(key) != sizeof(stored) ||
      preferences.getBytes(key, &stored, sizeof(stored)) != sizeof(stored) ||
      stored.magic != STORAGE_MAGIC || stored.version != STORAGE_VERSION ||
      stored.active != 1 || stored.expiresAt == 0) {
    output = {};
    return false;
  }
  output = {true, stored.initialState != 0, stored.expireState != 0,
            stored.expiresAt};
  return true;
}

bool saveTimeout(uint8_t channel,
                 const RelayContract::TimeoutConfig& config) {
  char key[3];
  makeKey('t', channel, key);
  if (!config.active) {
    return !preferences.isKey(key) || preferences.remove(key);
  }
  StoredTimeout stored{STORAGE_MAGIC, STORAGE_VERSION, 1,
                       static_cast<uint8_t>(config.initialState),
                       static_cast<uint8_t>(config.expireState), {},
                       config.expiresAt};
  return preferences.putBytes(key, &stored, sizeof(stored)) == sizeof(stored);
}

bool loadIntervalSchedule(uint8_t channel,
                          RelayContract::IntervalScheduleConfig& output) {
  resetIntervalSchedule(output);
  if (!isValidChannel(channel)) return false;

  char key[3];
  makeKey('i', channel, key);
  StoredIntervalSchedule stored{};
  if (preferences.getBytesLength(key) != sizeof(stored) ||
      preferences.getBytes(key, &stored, sizeof(stored)) != sizeof(stored) ||
      stored.magic != STORAGE_MAGIC ||
      stored.version != INTERVAL_STORAGE_VERSION || stored.enabled > 1 ||
      stored.entryCount > RelayContract::MAX_SCHEDULE_EVENTS)
    return false;

  output.enabled = stored.enabled != 0;
  output.entryCount = stored.entryCount;
  for (uint8_t i = 0; i < stored.entryCount; ++i) {
    const auto& storedEntry = stored.entries[i];
    if (storedEntry.id == 0 || storedEntry.startAt == 0 ||
        storedEntry.endAt == 0 || storedEntry.startAt >= storedEntry.endAt ||
        !isValidIntervalStatus(storedEntry.status)) {
      resetIntervalSchedule(output);
      return false;
    }
    for (uint8_t j = 0; j < i; ++j) {
      if (output.entries[j].id == storedEntry.id) {
        resetIntervalSchedule(output);
        return false;
      }
    }
    output.entries[i].id = storedEntry.id;
    output.entries[i].startAt = storedEntry.startAt;
    output.entries[i].endAt = storedEntry.endAt;
    output.entries[i].status =
        static_cast<RelayContract::IntervalScheduleStatus>(storedEntry.status);
  }
  return true;
}

bool saveIntervalSchedule(
    uint8_t channel, const RelayContract::IntervalScheduleConfig& config) {
  if (!isValidChannel(channel) || !hasValidIntervalEntries(config))
    return false;

  char key[3];
  makeKey('i', channel, key);
  StoredIntervalSchedule stored{};
  stored.magic = STORAGE_MAGIC;
  stored.version = INTERVAL_STORAGE_VERSION;
  stored.enabled = static_cast<uint8_t>(config.enabled);
  stored.entryCount = config.entryCount;
  for (uint8_t i = 0; i < config.entryCount; ++i) {
    const auto& entry = config.entries[i];
    stored.entries[i].id = entry.id;
    stored.entries[i].status = static_cast<uint8_t>(entry.status);
    stored.entries[i].startAt = entry.startAt;
    stored.entries[i].endAt = entry.endAt;
  }
  return preferences.putBytes(key, &stored, sizeof(stored)) == sizeof(stored);
}

}  // namespace AutomationStorage
