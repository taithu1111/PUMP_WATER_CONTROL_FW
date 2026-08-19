#include "automation_time.h"

#include <RTClib.h>
#include <WiFi.h>
#include <Wire.h>
#include <time.h>

#include "app_config.h"
#include "log.h"

namespace AutomationTime {
namespace {

constexpr time_t MIN_VALID_EPOCH = 1704067200;

RTC_DS1307 rtc;
bool rtcReady = false;
bool valid = false;
bool ntpRequested = false;
bool ntpSynchronized = false;
time_t baseEpoch = 0;
uint32_t baseMillis = 0;
uint32_t lastRtcRefreshMs = 0;
uint32_t lastNtpRequestMs = 0;
uint32_t lastRtcSyncMs = 0;

bool elapsed(uint32_t now, uint32_t previous, uint32_t interval) {
  return static_cast<uint32_t>(now - previous) >= interval;
}

void cacheEpoch(time_t epoch) {
  if (epoch < MIN_VALID_EPOCH) return;
  baseEpoch = epoch;
  baseMillis = millis();
  valid = true;
}

time_t currentEpoch() {
  if (!valid) return 0;
  return baseEpoch + static_cast<time_t>(
                         static_cast<uint32_t>(millis() - baseMillis) / 1000U);
}

bool readRtc() {
  if (!rtcReady || !rtc.isrunning()) return false;
  const time_t epoch = static_cast<time_t>(rtc.now().unixtime());
  if (epoch < MIN_VALID_EPOCH) return false;
  cacheEpoch(epoch);
  return true;
}

void requestNtp(uint32_t now) {
  configTzTime(AppConfig::Network::TIMEZONE_POSIX,
               AppConfig::Network::NTP_SERVER_PRIMARY,
               AppConfig::Network::NTP_SERVER_SECONDARY);
  ntpRequested = true;
  lastNtpRequestMs = now;
  LOG_PRINTLN("[time] NTP synchronization requested");
}

void acceptSystemTime(time_t epoch, uint32_t now) {
  cacheEpoch(epoch);
  ntpSynchronized = true;
  if (rtcReady) {
    rtc.adjust(DateTime(static_cast<uint32_t>(epoch)));
    lastRtcSyncMs = now;
    LOG_PRINTLN("[time] NTP synchronized; DS1307 updated");
  } else {
    LOG_PRINTLN("[time] NTP synchronized; DS1307 unavailable");
  }
}

}  // namespace

bool begin() {
  setenv("TZ", AppConfig::Network::TIMEZONE_POSIX, 1);
  tzset();
  Wire1.begin(AppConfig::Hardware::RTC_I2C_SDA_PIN,
              AppConfig::Hardware::RTC_I2C_SCL_PIN,
              AppConfig::Hardware::RTC_I2C_FREQUENCY_HZ);
  rtcReady = rtc.begin(&Wire1);
  if (!rtcReady) {
    LOG_PRINTLN("[time] DS1307 not responding; waiting for NTP");
    return false;
  }
  LOG_PRINTLN(readRtc() ? "[time] Time restored from DS1307"
                        : "[time] DS1307 time invalid; waiting for NTP");
  return true;
}

void loop() {
  const uint32_t now = millis();
  const bool wifiConnected = WiFi.status() == WL_CONNECTED;
  if (wifiConnected &&
      (!ntpRequested || (!ntpSynchronized &&
                         elapsed(now, lastNtpRequestMs,
                                 AppConfig::Network::NTP_RETRY_INTERVAL_MS)))) {
    requestNtp(now);
  }

  if (wifiConnected) {
    const time_t systemNow = time(nullptr);
    if (systemNow >= MIN_VALID_EPOCH &&
        (!ntpSynchronized ||
         elapsed(now, lastRtcSyncMs, AppConfig::Network::RTC_SYNC_INTERVAL_MS))) {
      acceptSystemTime(systemNow, now);
      return;
    }
  }

  if (!ntpSynchronized && rtcReady &&
      elapsed(now, lastRtcRefreshMs,
              AppConfig::Network::RTC_REFRESH_INTERVAL_MS)) {
    lastRtcRefreshMs = now;
    readRtc();
  }
}

RelayContract::TimeSnapshot snapshot() {
  RelayContract::TimeSnapshot result{};
  const time_t epoch = currentEpoch();
  if (epoch == 0) return result;
  result.valid = true;
  result.epochSeconds = static_cast<uint64_t>(epoch);
  return result;
}

}  // namespace AutomationTime
