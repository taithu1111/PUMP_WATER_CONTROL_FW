#include "operating_mode.h"

#include <Arduino.h>
#include <Preferences.h>

#include <utility>

#include "app_config.h"
#include "log.h"

namespace OperatingMode {
namespace {

constexpr uint8_t ONLINE_NVS_VALUE = 0;
constexpr uint8_t OFFLINE_NVS_VALUE = 1;

ChangeHandler notifyChange;
Mode activeMode = Mode::Offline;
Mode candidateMode = Mode::Offline;
Mode persistedMode = Mode::Offline;
uint32_t candidateSinceMs = 0;
bool settled = false;
bool persistedModeKnown = false;
bool started = false;

Mode readPinMode() {
  return digitalRead(AppConfig::Hardware::OPERATING_MODE_PIN) == LOW
             ? Mode::Online
             : Mode::Offline;
}

uint8_t nvsValue(Mode mode) {
  return mode == Mode::Online ? ONLINE_NVS_VALUE : OFFLINE_NVS_VALUE;
}

bool loadPersistedMode() {
  Preferences preferences;
  if (!preferences.begin(AppConfig::System::OPERATING_MODE_NVS_NAMESPACE,
                         true)) {
    LOG_PRINTLN("[mode] Failed to open NVS for reading");
    return false;
  }

  if (!preferences.isKey(AppConfig::System::OPERATING_MODE_NVS_KEY)) {
    preferences.end();
    return false;
  }

  const uint8_t stored = preferences.getUChar(
      AppConfig::System::OPERATING_MODE_NVS_KEY, OFFLINE_NVS_VALUE);
  preferences.end();
  if (stored > OFFLINE_NVS_VALUE) {
    LOG_PRINTLN("[mode] Ignoring invalid NVS value");
    return false;
  }

  persistedMode = stored == ONLINE_NVS_VALUE ? Mode::Online : Mode::Offline;
  return true;
}

bool savePersistedMode(Mode mode) {
  Preferences preferences;
  if (!preferences.begin(AppConfig::System::OPERATING_MODE_NVS_NAMESPACE,
                         false)) {
    LOG_PRINTLN("[mode] Failed to open NVS for writing");
    return false;
  }

  const size_t written = preferences.putUChar(
      AppConfig::System::OPERATING_MODE_NVS_KEY, nvsValue(mode));
  preferences.end();
  if (written != sizeof(uint8_t)) {
    LOG_PRINTLN("[mode] Failed to persist operating mode");
    return false;
  }

  persistedMode = mode;
  persistedModeKnown = true;
  return true;
}

void acceptCandidate() {
  activeMode = candidateMode;
  settled = true;
  if (!persistedModeKnown || persistedMode != activeMode) {
    savePersistedMode(activeMode);
  }

  LOG_PRINTF("[mode] GPIO stable %s -> %s\n",
             activeMode == Mode::Online ? "LOW" : "HIGH",
             activeMode == Mode::Online ? "ONLINE" : "OFFLINE");
  if (notifyChange) notifyChange(activeMode);
}

}  // namespace

bool begin(ChangeHandler changeHandler) {
  if (started) return true;

  notifyChange = std::move(changeHandler);
  pinMode(AppConfig::Hardware::OPERATING_MODE_PIN, INPUT_PULLUP);
  candidateMode = readPinMode();
  candidateSinceMs = millis();
  persistedModeKnown = loadPersistedMode();
  started = true;
  return true;
}

void loop() {
  if (!started) return;

  const Mode observed = readPinMode();
  const uint32_t now = millis();
  if (observed != candidateMode) {
    candidateMode = observed;
    candidateSinceMs = now;
    return;
  }

  if (static_cast<uint32_t>(now - candidateSinceMs) <
          AppConfig::Hardware::OPERATING_MODE_DEBOUNCE_MS ||
      (settled && activeMode == candidateMode)) {
    return;
  }

  acceptCandidate();
}

Mode current() {
  return activeMode;
}

}  // namespace OperatingMode
