#include "automation_storage.h"

#include <Preferences.h>

namespace AutomationStorage {
namespace {

constexpr char NVS_NAMESPACE[] = "relay_auto";
constexpr uint32_t STORAGE_MAGIC = 0x52415554;
constexpr uint16_t STORAGE_VERSION = 1;

struct StoredTimeout {
  uint32_t magic;
  uint16_t version;
  uint8_t active;
  uint8_t initialState;
  uint8_t expireState;
  uint8_t reserved[3];
  uint64_t expiresAt;
};

struct StoredScheduleEvent {
  uint8_t id;
  uint8_t daysMask;
  uint16_t minuteOfDay;
  uint8_t state;
  uint8_t reserved[3];
};

struct StoredSchedule {
  uint32_t magic;
  uint16_t version;
  uint8_t enabled;
  uint8_t eventCount;
  StoredScheduleEvent events[RelayContract::MAX_SCHEDULE_EVENTS];
};

Preferences preferences;

void makeKey(char prefix, uint8_t channel, char key[3]) {
  key[0] = prefix;
  key[1] = static_cast<char>('0' + channel);
  key[2] = '\0';
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

bool loadSchedule(uint8_t channel, RelayContract::ScheduleConfig& output) {
  char key[3];
  makeKey('s', channel, key);
  StoredSchedule stored{};
  if (preferences.getBytesLength(key) != sizeof(stored) ||
      preferences.getBytes(key, &stored, sizeof(stored)) != sizeof(stored) ||
      stored.magic != STORAGE_MAGIC || stored.version != STORAGE_VERSION ||
      stored.eventCount > RelayContract::MAX_SCHEDULE_EVENTS) {
    output = {};
    return false;
  }
  output.enabled = stored.enabled != 0;
  output.eventCount = stored.eventCount;
  for (uint8_t i = 0; i < output.eventCount; ++i) {
    output.events[i] = {stored.events[i].id, stored.events[i].daysMask,
                        stored.events[i].minuteOfDay,
                        stored.events[i].state != 0};
  }
  return true;
}

bool saveSchedule(uint8_t channel,
                  const RelayContract::ScheduleConfig& config) {
  char key[3];
  makeKey('s', channel, key);
  StoredSchedule stored{};
  stored.magic = STORAGE_MAGIC;
  stored.version = STORAGE_VERSION;
  stored.enabled = config.enabled;
  stored.eventCount = config.eventCount;
  for (uint8_t i = 0; i < config.eventCount; ++i) {
    stored.events[i] = {config.events[i].id, config.events[i].daysMask,
                        config.events[i].minuteOfDay,
                        static_cast<uint8_t>(config.events[i].state), {}};
  }
  return preferences.putBytes(key, &stored, sizeof(stored)) == sizeof(stored);
}

}  // namespace AutomationStorage
