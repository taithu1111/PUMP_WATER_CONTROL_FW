#include "mqtt_relay_gateway.h"

#include <cstring>
#include <utility>

#include "app_config.h"
#include "log.h"
#include "mqtt_connection.h"
#include "mqtt_protocol.h"

namespace MqttRelayGateway {
namespace {

constexpr uint8_t MESSAGE_QUEUE_SIZE = 4;

struct RawMessage {
  char topic[MqttProtocol::TOPIC_SIZE];
  uint8_t payload[AppConfig::Network::MQTT_INCOMING_BUFFER_SIZE];
  uint16_t payloadLength;
};

CommandHandler handleCommand;
AutomationSnapshotProvider provideAutomation;
RelayStateProvider provideRelays;
MqttProtocol::Topics topics;
RawMessage messageQueue[MESSAGE_QUEUE_SIZE];
char publishBuffer[AppConfig::Network::MQTT_PACKET_BUFFER_SIZE];
uint8_t queueHead = 0;
uint8_t queueTail = 0;
uint8_t queueCount = 0;

void enqueueMessage(const char* topic, const uint8_t* payload, size_t length) {
  if (queueCount >= MESSAGE_QUEUE_SIZE ||
      length >= AppConfig::Network::MQTT_INCOMING_BUFFER_SIZE) {
    LOG_PRINTLN("[mqtt] Incoming message dropped");
    return;
  }
  const size_t topicLength = strnlen(topic, MqttProtocol::TOPIC_SIZE);
  if (topicLength >= MqttProtocol::TOPIC_SIZE) {
    LOG_PRINTLN("[mqtt] Incoming topic is too long");
    return;
  }
  RawMessage& message = messageQueue[queueTail];
  memcpy(message.topic, topic, topicLength);
  message.topic[topicLength] = '\0';
  memcpy(message.payload, payload, length);
  message.payloadLength = static_cast<uint16_t>(length);
  queueTail = (queueTail + 1) % MESSAGE_QUEUE_SIZE;
  ++queueCount;
}

bool publishPayload(const char* topic, size_t length, bool retained = true) {
  return length > 0 && length < sizeof(publishBuffer) &&
         MqttConnection::publish(
             topic, reinterpret_cast<const uint8_t*>(publishBuffer), length,
             retained);
}

bool publishCommandAck(uint8_t channel,
                       RelayContract::CommandResult result) {
  return publishPayload(
      topics.commandAck,
      MqttProtocol::encodeCommandAck(channel, result, publishBuffer,
                                     sizeof(publishBuffer)),
      false);
}

bool publishRelayState() {
  if (!provideRelays) return false;
  bool states[AppConfig::System::OUTLET_COUNT]{};
  provideRelays(states);
  return publishPayload(
      topics.state,
      MqttProtocol::encodeRelayStates(states, publishBuffer,
                                      sizeof(publishBuffer)));
}

bool readAutomation(RelayContract::AutomationSnapshot& snapshot) {
  return provideAutomation && provideAutomation(snapshot);
}

bool publishTimeoutState() {
  RelayContract::AutomationSnapshot snapshot{};
  if (!readAutomation(snapshot)) return false;
  return publishPayload(
      topics.timeoutState,
      MqttProtocol::encodeTimeoutStates(snapshot, publishBuffer,
                                        sizeof(publishBuffer)));
}

bool publishScheduleState() {
  RelayContract::AutomationSnapshot snapshot{};
  if (!readAutomation(snapshot)) return false;
  return publishPayload(
      topics.scheduleState,
      MqttProtocol::encodeOneShotScheduleStates(
          snapshot, snapshot.currentEpoch, publishBuffer,
          sizeof(publishBuffer)));
}

bool initializeSession() {
  const bool subscribedSet = MqttConnection::subscribe(topics.set, 1);
  const bool subscribedGet = MqttConnection::subscribe(topics.get, 1);
  const bool subscribedTimeoutSet =
      MqttConnection::subscribe(topics.timeoutSet, 1);
  const bool subscribedTimeoutGet =
      MqttConnection::subscribe(topics.timeoutGet, 1);
  const bool subscribedScheduleSet =
      MqttConnection::subscribe(topics.scheduleSet, 1);
  const bool subscribedScheduleGet =
      MqttConnection::subscribe(topics.scheduleGet, 1);
  const bool online = MqttConnection::publish(topics.status, "online", true);
  if (!subscribedSet || !subscribedGet || !subscribedTimeoutSet ||
      !subscribedTimeoutGet || !subscribedScheduleSet ||
      !subscribedScheduleGet || !online) return false;
  publishRelayState();
  publishTimeoutState();
  publishScheduleState();
  return true;
}

void dispatchCommand(const RawMessage& message) {
  RelayContract::Command command{};
  bool valid = false;
  if (strcmp(message.topic, topics.set) == 0) {
    valid = MqttProtocol::decodeSetCommand(message.payload,
                                           message.payloadLength, command);
  } else if (strcmp(message.topic, topics.timeoutSet) == 0) {
    valid = MqttProtocol::decodeTimeoutCommand(message.payload,
                                               message.payloadLength, command);
  } else if (strcmp(message.topic, topics.scheduleSet) == 0) {
    valid = MqttProtocol::decodeOneShotScheduleCommand(
        message.payload, message.payloadLength, command);
  }
  RelayContract::CommandResult result =
      RelayContract::CommandResult::InvalidArgument;
  if (valid && handleCommand) {
    result = handleCommand(command);
  } else if (valid) {
    result = RelayContract::CommandResult::NotStarted;
  } else {
    LOG_PRINTLN("[mqtt] Invalid command payload");
  }
  publishCommandAck(valid ? command.channel : 0, result);
}

void processQueuedMessages() {
  while (queueCount > 0) {
    const RawMessage& message = messageQueue[queueHead];
    if (strcmp(message.topic, topics.get) == 0) {
      publishRelayState();
    } else if (strcmp(message.topic, topics.timeoutGet) == 0) {
      publishTimeoutState();
    } else if (strcmp(message.topic, topics.scheduleGet) == 0) {
      publishScheduleState();
    } else {
      dispatchCommand(message);
    }
    queueHead = (queueHead + 1) % MESSAGE_QUEUE_SIZE;
    --queueCount;
  }
}

}  // namespace

bool begin(const char* ssid, const char* password,
           CommandHandler commandHandler,
           AutomationSnapshotProvider automationProvider,
           RelayStateProvider relayProvider) {
  stop();
  handleCommand = std::move(commandHandler);
  provideAutomation = std::move(automationProvider);
  provideRelays = std::move(relayProvider);
  MqttProtocol::buildTopics(topics);
  if (MqttConnection::begin(ssid, password, topics.status, enqueueMessage,
                            initializeSession)) return true;
  handleCommand = nullptr;
  provideAutomation = nullptr;
  provideRelays = nullptr;
  return false;
}

void loop() {
  MqttConnection::loop();
  processQueuedMessages();
}

void stop() {
  MqttConnection::stop();
  handleCommand = nullptr;
  provideAutomation = nullptr;
  provideRelays = nullptr;
  queueHead = 0;
  queueTail = 0;
  queueCount = 0;
}

bool isConnected() {
  return MqttConnection::isConnected();
}

void notifyStateChange(uint8_t, uint8_t) {
  publishRelayState();
  publishTimeoutState();
  publishScheduleState();
}

}  // namespace MqttRelayGateway
