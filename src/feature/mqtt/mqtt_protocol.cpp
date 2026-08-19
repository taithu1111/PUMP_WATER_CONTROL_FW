#include "mqtt_protocol.h"

#include <ArduinoJson.h>
#include <cstring>

#include "mqtt_schedule_datetime.h"

namespace MqttProtocol {

void buildTopics(Topics& topics) {
  snprintf(topics.set, sizeof(topics.set), "%s/relay/set",
           AppConfig::Network::MQTT_TOPIC_ROOT);
  snprintf(topics.get, sizeof(topics.get), "%s/relay/get",
           AppConfig::Network::MQTT_TOPIC_ROOT);
  snprintf(topics.state, sizeof(topics.state), "%s/relay/state",
           AppConfig::Network::MQTT_TOPIC_ROOT);
  snprintf(topics.timeoutSet, sizeof(topics.timeoutSet), "%s/relay/timeout/set",
           AppConfig::Network::MQTT_TOPIC_ROOT);
  snprintf(topics.timeoutGet, sizeof(topics.timeoutGet), "%s/relay/timeout/get",
           AppConfig::Network::MQTT_TOPIC_ROOT);
  snprintf(topics.timeoutState, sizeof(topics.timeoutState), "%s/relay/timeout/state",
           AppConfig::Network::MQTT_TOPIC_ROOT);
  snprintf(topics.scheduleSet, sizeof(topics.scheduleSet), "%s/relay/schedule/set",
           AppConfig::Network::MQTT_TOPIC_ROOT);
  snprintf(topics.scheduleGet, sizeof(topics.scheduleGet), "%s/relay/schedule/get",
           AppConfig::Network::MQTT_TOPIC_ROOT);
  snprintf(topics.scheduleState, sizeof(topics.scheduleState), "%s/relay/schedule/state",
           AppConfig::Network::MQTT_TOPIC_ROOT);
  snprintf(topics.status, sizeof(topics.status), "%s/status",
           AppConfig::Network::MQTT_TOPIC_ROOT);
}

bool decodeTimeoutCommand(const uint8_t* payload, size_t length,
                          RelayContract::Command& command) {
  JsonDocument document;
  if (deserializeJson(document, payload, length) ||
      !document["channel"].is<int>()) return false;
  const int channel = document["channel"].as<int>();
  if (channel < 1 || channel > AppConfig::System::OUTLET_COUNT) return false;
  command.channel = static_cast<uint8_t>(channel);
  if (document["cancel"].is<bool>() && document["cancel"].as<bool>()) {
    command.type = RelayContract::CommandType::CancelTimeout;
    return true;
  }
  if (!document["state"].is<bool>() || !document["durationSec"].is<uint32_t>() ||
      !document["onExpire"].is<bool>()) return false;
  command.type = RelayContract::CommandType::SetTimeout;
  command.state = document["state"].as<bool>();
  command.durationSeconds = document["durationSec"].as<uint32_t>();
  command.expireState = document["onExpire"].as<bool>();
  return command.durationSeconds > 0;
}

bool decodeScheduleCommand(const uint8_t* payload, size_t length,
                           RelayContract::Command& command) {
  JsonDocument document;
  if (deserializeJson(document, payload, length) ||
      !document["channel"].is<int>() || !document["enabled"].is<bool>()) return false;
  const int channel = document["channel"].as<int>();
  if (channel < 1 || channel > AppConfig::System::OUTLET_COUNT) return false;
  command.type = RelayContract::CommandType::SetSchedule;
  command.channel = static_cast<uint8_t>(channel);
  command.schedule.enabled = document["enabled"].as<bool>();
  if (!command.schedule.enabled) return true;
  if (!document["events"].is<JsonArray>()) return false;
  JsonArray events = document["events"].as<JsonArray>();
  if (events.size() == 0 || events.size() > RelayContract::MAX_SCHEDULE_EVENTS) return false;
  for (JsonObject event : events) {
    if (!event["id"].is<int>() || !event["days"].is<JsonArray>() ||
        !event["time"].is<const char*>() || !event["state"].is<bool>()) return false;
    const int id = event["id"].as<int>();
    const char* time = event["time"].as<const char*>();
    if (id < 1 || id > 255 || strlen(time) != 5 || time[2] != ':' ||
        time[0] < '0' || time[0] > '9' || time[1] < '0' || time[1] > '9' ||
        time[3] < '0' || time[3] > '9' || time[4] < '0' || time[4] > '9') return false;
    const uint8_t hour = static_cast<uint8_t>((time[0] - '0') * 10 + time[1] - '0');
    const uint8_t minute = static_cast<uint8_t>((time[3] - '0') * 10 + time[4] - '0');
    if (hour > 23 || minute > 59) return false;
    uint8_t daysMask = 0;
    for (JsonVariant dayValue : event["days"].as<JsonArray>()) {
      if (!dayValue.is<int>()) return false;
      const int day = dayValue.as<int>();
      if (day < 1 || day > 7) return false;
      daysMask |= static_cast<uint8_t>(1U << (day - 1));
    }
    if (daysMask == 0) return false;
    auto& output = command.schedule.events[command.schedule.eventCount++];
    output = {static_cast<uint8_t>(id), daysMask,
              static_cast<uint16_t>(hour * 60 + minute), event["state"].as<bool>()};
  }
  return true;
}

bool decodeOneShotScheduleCommand(const uint8_t* payload, size_t length,
                                  RelayContract::Command& command) {
  JsonDocument document;
  if (deserializeJson(document, payload, length) ||
      !document["channel"].is<int>() ||
      !document["action"].is<const char*>()) return false;
  const int channel = document["channel"].as<int>();
  if (channel < 1 || channel > AppConfig::System::OUTLET_COUNT) return false;
  command.channel = static_cast<uint8_t>(channel);

  const char* action = document["action"].as<const char*>();
  if (strcmp(action, "delete") == 0) {
    if (!document["eventId"].is<int>()) return false;
    const int eventId = document["eventId"].as<int>();
    if (eventId < 1 || eventId > 255) return false;
    command.type = RelayContract::CommandType::DeleteOneShotSchedule;
    command.scheduleEventId = static_cast<uint8_t>(eventId);
    return true;
  }

  if (strcmp(action, "set_enabled") == 0) {
    if (!document["enabled"].is<bool>()) return false;
    command.type = RelayContract::CommandType::SetOneShotScheduleEnabled;
    command.scheduleEnabled = document["enabled"].as<bool>();
    return true;
  }

  if (strcmp(action, "upsert") != 0 ||
      !document["event"].is<JsonObject>()) return false;
  JsonObject event = document["event"].as<JsonObject>();
  if (!event["id"].is<int>() || !event["day"].is<const char*>() ||
      !event["dueDate"].is<const char*>() || !event["state"].is<bool>())
    return false;
  const int eventId = event["id"].as<int>();
  uint64_t dueAt = 0;
  if (eventId < 1 || eventId > 255 ||
      !MqttScheduleDateTime::parse(event["day"].as<const char*>(),
                                  event["dueDate"].as<const char*>(),
                                  dueAt)) return false;
  command.type = RelayContract::CommandType::UpsertOneShotSchedule;
  command.oneShotEvent = {
      static_cast<uint8_t>(eventId), dueAt, event["state"].as<bool>(),
      RelayContract::OneShotScheduleStatus::Pending};
  return true;
}

bool decodeSetCommand(const uint8_t* payload,
                      size_t length,
                      RelayContract::Command& command) {
  JsonDocument document;
  const DeserializationError error = deserializeJson(document, payload, length);

  if (error || !document["channel"].is<int>() || !document["state"].is<bool>()) {
    return false;
  }

  const int channel = document["channel"].as<int>();
  if (channel < 1 || channel > AppConfig::System::OUTLET_COUNT) {
    return false;
  }

  command.type = RelayContract::CommandType::SetChannel;
  command.channel = static_cast<uint8_t>(channel);
  command.state = document["state"].as<bool>();
  return true;
}

size_t encodeRelayStates(
    const bool states[AppConfig::System::OUTLET_COUNT],
    char* output,
    size_t outputSize) {
  if (states == nullptr || output == nullptr || outputSize == 0) {
    return 0;
  }

  JsonDocument document;
  JsonArray relays = document["relays"].to<JsonArray>();
  for (uint8_t index = 0; index < AppConfig::System::OUTLET_COUNT; ++index) {
    relays.add(states[index]);
  }

  return serializeJson(document, output, outputSize);
}

size_t encodeTimeoutStates(
    const RelayContract::AutomationSnapshot& snapshot, char* output,
    size_t outputSize) {
  JsonDocument document;
  JsonArray items = document["timeouts"].to<JsonArray>();
  for (uint8_t index = 0; index < AppConfig::System::OUTLET_COUNT; ++index) {
    JsonObject item = items.add<JsonObject>();
    item["channel"] = index + 1;
    const auto& state = snapshot.timeouts[index];
    item["active"] = state.active;
    if (state.active) {
      item["state"] = state.initialState;
      item["onExpire"] = state.expireState;
      item["expiresAt"] = state.expiresAt;
    }
  }
  return serializeJson(document, output, outputSize);
}

size_t encodeScheduleStates(
    const RelayContract::AutomationSnapshot& snapshot, char* output,
    size_t outputSize) {
  JsonDocument document;
  JsonArray schedules = document["schedules"].to<JsonArray>();
  for (uint8_t index = 0; index < AppConfig::System::OUTLET_COUNT; ++index) {
    JsonObject item = schedules.add<JsonObject>();
    item["channel"] = index + 1;
    const auto& state = snapshot.schedules[index];
    item["enabled"] = state.enabled;
    JsonArray events = item["events"].to<JsonArray>();
    for (uint8_t i = 0; i < state.eventCount; ++i) {
      const auto& source = state.events[i];
      JsonObject event = events.add<JsonObject>();
      event["id"] = source.id;
      JsonArray days = event["days"].to<JsonArray>();
      for (uint8_t day = 1; day <= 7; ++day) {
        if ((source.daysMask & (1U << (day - 1))) != 0) days.add(day);
      }
      char time[6];
      snprintf(time, sizeof(time), "%02u:%02u", source.minuteOfDay / 60,
               source.minuteOfDay % 60);
      event["time"] = time;
      event["state"] = source.state;
    }
  }
  return serializeJson(document, output, outputSize);
}

size_t encodeOneShotScheduleStates(
    const RelayContract::AutomationSnapshot& snapshot, uint64_t nowEpoch,
    char* output, size_t outputSize) {
  JsonDocument document;
  JsonArray schedules = document["schedules"].to<JsonArray>();
  for (uint8_t index = 0; index < AppConfig::System::OUTLET_COUNT; ++index) {
    JsonObject item = schedules.add<JsonObject>();
    item["channel"] = index + 1;
    const auto& schedule = snapshot.oneShotSchedules[index];
    item["enabled"] = schedule.enabled;
    JsonArray events = item["events"].to<JsonArray>();
    for (uint8_t i = 0; i < schedule.eventCount; ++i) {
      const auto& source = schedule.events[i];
      JsonObject event = events.add<JsonObject>();
      event["id"] = source.id;
      char day[11];
      char dueDate[26];
      MqttScheduleDateTime::format(source.dueAt, day, dueDate);
      event["day"] = day;
      event["dueDate"] = dueDate;
      event["state"] = source.state;
      if (source.status == RelayContract::OneShotScheduleStatus::Executed) {
        event["status"] = "executed";
      } else if (schedule.enabled && snapshot.timeouts[index].active &&
                 source.dueAt <= nowEpoch) {
        event["status"] = "blocked_by_timeout";
      } else {
        event["status"] = "pending";
      }
    }
  }
  return serializeJson(document, output, outputSize);
}

}  // namespace MqttProtocol
