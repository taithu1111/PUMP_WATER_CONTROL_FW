#pragma once

#include <Arduino.h>

namespace AppConfig {

namespace System {
constexpr uint8_t OUTLET_COUNT = 4;
constexpr char BLE_DEVICE_NAME[] = "PUMP-CONTROL";
}  // namespace System

namespace Hardware {
constexpr uint8_t PCF8575_ADDRESS = 0x20;
constexpr int8_t I2C_SDA_PIN = 21;
constexpr int8_t I2C_SCL_PIN = 20;
constexpr uint32_t I2C_FREQUENCY_HZ = 100000;

constexpr uint8_t DS1307_ADDRESS = 0x68;
constexpr int8_t RTC_I2C_SDA_PIN = 4;
constexpr int8_t RTC_I2C_SCL_PIN = 5;
constexpr uint32_t RTC_I2C_FREQUENCY_HZ = 100000;

constexpr uint8_t RELAY_PCF_PORTS[System::OUTLET_COUNT] = {1, 2, 3, 4};
constexpr bool RELAY_ACTIVE_LOW = true;
}  // namespace Hardware

namespace Network {
constexpr char DEVICE_ID[] = "pump-controller-01";
constexpr char MQTT_HOST[] = "mqtt.agribeacon.tech";
constexpr uint16_t MQTT_PORT = 8883;
constexpr char MQTT_TOPIC_ROOT[] = "pump/pump-controller-01";

// Chỉ dùng tạm khi chưa cung cấp CA certificate của broker.
constexpr bool MQTT_TLS_INSECURE = true;
constexpr uint32_t WIFI_RECONNECT_INTERVAL_MS = 10000;
constexpr uint32_t MQTT_RECONNECT_INTERVAL_MS = 5000;
constexpr uint16_t MQTT_INCOMING_BUFFER_SIZE = 4096;
constexpr uint16_t MQTT_PACKET_BUFFER_SIZE = 4096;

constexpr char NTP_SERVER_PRIMARY[] = "pool.ntp.org";
constexpr char NTP_SERVER_SECONDARY[] = "time.google.com";
// POSIX TZ syntax: UTC+7 is written as -7.
constexpr char TIMEZONE_POSIX[] = "ICT-7";
constexpr uint32_t NTP_RETRY_INTERVAL_MS = 30000;
constexpr uint32_t RTC_REFRESH_INTERVAL_MS = 1000;
constexpr uint32_t RTC_SYNC_INTERVAL_MS = 6UL * 60UL * 60UL * 1000UL;
}  // namespace Network

}  // namespace AppConfig
