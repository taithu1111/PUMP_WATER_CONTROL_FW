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
  snprintf(topics.commandAck, sizeof(topics.commandAck), "%s/relay/command/ack",
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

bool decodeIntervalScheduleCommand(const uint8_t* payload, size_t length,
                                   RelayContract::Command& command) {
  JsonDocument document;
  if (deserializeJson(document, payload, length) ||
      !document["channel"].is<int>() ||
      !document["action"].is<const char*>())
    return false;
  const int channel = document["channel"].as<int>();
  if (channel < 1 || channel > AppConfig::System::OUTLET_COUNT) return false;
  command.channel = static_cast<uint8_t>(channel);

  const char* action = document["action"].as<const char*>();
  if (strcmp(action, "delete") == 0) {
    if (!document["scheduleId"].is<int>()) return false;
    const int scheduleId = document["scheduleId"].as<int>();
    if (scheduleId < 1 || scheduleId > 255) return false;
    command.type = RelayContract::CommandType::DeleteIntervalSchedule;
    command.intervalScheduleId = static_cast<uint8_t>(scheduleId);
    return true;
  }

  if (strcmp(action, "set_enabled") == 0) {
    if (!document["enabled"].is<bool>()) return false;
    command.type = RelayContract::CommandType::SetIntervalScheduleEnabled;
    command.intervalScheduleEnabled = document["enabled"].as<bool>();
    return true;
  }

  if (strcmp(action, "upsert") != 0 ||
      !document["schedule"].is<JsonObject>())
    return false;
  JsonObject schedule = document["schedule"].as<JsonObject>();
  if (!schedule["id"].is<int>() ||
      !schedule["startAt"].is<const char*>() ||
      !schedule["endAt"].is<const char*>())
    return false;
  const int scheduleId = schedule["id"].as<int>();
  uint64_t startAt = 0;
  uint64_t endAt = 0;
  if (scheduleId < 1 || scheduleId > 255 ||
      !MqttScheduleDateTime::parseTimestamp(
          schedule["startAt"].as<const char*>(), startAt) ||
      !MqttScheduleDateTime::parseTimestamp(
          schedule["endAt"].as<const char*>(), endAt) ||
      startAt >= endAt)
    return false;
  command.type = RelayContract::CommandType::UpsertIntervalSchedule;
  command.intervalSchedule = {
      static_cast<uint8_t>(scheduleId), startAt, endAt,
      RelayContract::IntervalScheduleStatus::Pending};
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

size_t encodeIntervalScheduleStates(
    const RelayContract::AutomationSnapshot& snapshot, uint64_t nowEpoch,
    char* output, size_t outputSize) {
  JsonDocument document;
  JsonArray schedules = document["schedules"].to<JsonArray>();
  for (uint8_t index = 0; index < AppConfig::System::OUTLET_COUNT; ++index) {
    JsonObject item = schedules.add<JsonObject>();
    item["channel"] = index + 1;
    const auto& config = snapshot.intervalSchedules[index];
    item["enabled"] = config.enabled;
    JsonArray entries = item["entries"].to<JsonArray>();
    for (uint8_t i = 0; i < config.entryCount; ++i) {
      const auto& source = config.entries[i];
      JsonObject entry = entries.add<JsonObject>();
      entry["id"] = source.id;
      char startAt[26];
      char endAt[26];
      MqttScheduleDateTime::formatTimestamp(source.startAt, startAt);
      MqttScheduleDateTime::formatTimestamp(source.endAt, endAt);
      entry["startAt"] = startAt;
      entry["endAt"] = endAt;

      const char* status = "pending";
      if (source.status == RelayContract::IntervalScheduleStatus::Completed) {
        status = "completed";
      } else if (source.status ==
                 RelayContract::IntervalScheduleStatus::Missed) {
        status = "missed";
      } else if (config.enabled && snapshot.timeouts[index].active &&
                 nowEpoch >= source.startAt && nowEpoch < source.endAt) {
        status = "blocked_by_timeout";
      } else if (source.status ==
                 RelayContract::IntervalScheduleStatus::Active) {
        status = "active";
      }
      entry["status"] = status;
    }
  }
  return serializeJson(document, output, outputSize);
}

size_t encodeCommandAck(uint8_t channel, RelayContract::CommandResult result,
                        char* output, size_t outputSize) {
  if (output == nullptr || outputSize == 0) return 0;
  const char* resultText = "unknown";
  switch (result) {
    case RelayContract::CommandResult::Ok: resultText = "ok"; break;
    case RelayContract::CommandResult::InvalidChannel:
      resultText = "invalid_channel";
      break;
    case RelayContract::CommandResult::InvalidArgument:
      resultText = "invalid_argument";
      break;
    case RelayContract::CommandResult::TimeUnavailable:
      resultText = "time_unavailable";
      break;
    case RelayContract::CommandResult::StorageError:
      resultText = "storage_error";
      break;
    case RelayContract::CommandResult::RelayError:
      resultText = "relay_error";
      break;
    case RelayContract::CommandResult::NotStarted:
      resultText = "not_started";
      break;
  }
  JsonDocument document;
  document["channel"] = channel;
  document["ok"] = result == RelayContract::CommandResult::Ok;
  document["result"] = resultText;
  return serializeJson(document, output, outputSize);
}

}  // namespace MqttProtocol
