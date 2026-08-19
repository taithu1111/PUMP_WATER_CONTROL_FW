#pragma once
#include <stdint.h>
#include <stddef.h>
#include <functional>

// ── Channel IDs ───────────────────────────────────────────────────────
// Add new data types here; host and device must agree on the same values.
#define BLE_CH_AUTH     0x00   // Reserved: password authentication
#define BLE_CH_OTA      0x01   // OTA firmware update
#define BLE_CH_SENSOR   0x02   // Sensor data read / response
#define BLE_CH_PROVISION 0x03  // Wi-Fi and MQTT activation

// ── Packet format (BLE framing layer) ─────────────────────────────────
//   [CH:1][LEN_L:1][LEN_H:1][DATA:LEN]
//
// Each channel carries its own inner payload — no CRC at this layer
// (BLE PHY provides error correction; inner protocols add their own CRC
// if needed, e.g. OTA uses CRC-16/CCITT-FALSE on its inner packets).

// ── Types ─────────────────────────────────────────────────────────────
// Handler called when a complete packet for 'channel' arrives.
// data may be nullptr when len == 0.
using BleRxHandler = std::function<void(const uint8_t *data, size_t len)>;

// ── API ───────────────────────────────────────────────────────────────
// Call once in setup() before using any channel.
void ble_begin(const char *name);

// Stop advertising and disconnect the current client. A later ble_begin()
// restarts advertising without rebuilding the BLE service.
void ble_stop();

// Optional: called whenever a client connects / disconnects.
// When a password is set via ble_set_password(), the callback is only fired
// with connected=true AFTER the client sends the correct password on
// BLE_CH_AUTH. Disconnects always fire (if the client was authenticated).
void ble_set_connect_cb(std::function<void(bool connected)> cb);

// Optional: require authentication before a client is considered connected.
// Supply a verifier that receives the raw bytes the client sent on
// BLE_CH_AUTH and returns true iff they should be accepted. Pass nullptr to
// disable auth (clients connect without a challenge — the default).
//
// On success the device replies with "AUTH_OK"; on failure with "AUTH_FAIL".
// If a client does not authenticate within timeout_ms after connecting, the
// device drops the link. Pass 0 to disable the timeout. Default 10000 ms.
//
// Call before ble_begin() — or any time after, but a client already connected
// stays authenticated until it disconnects.
using BleAuthVerifier = std::function<bool(const uint8_t *data, size_t len)>;
void ble_set_auth_verifier(BleAuthVerifier verifier, uint32_t timeout_ms = 10000);

// Register a receive handler for a channel (call before ble_begin or in setup()).
void ble_register_channel(uint8_t channel, BleRxHandler handler);

// Send data on a channel (raw, không nén).
void ble_send(uint8_t channel, const uint8_t *data, size_t len);

// Pump the receive state machine — call from a FreeRTOS task or loop().
void ble_dispatch();

// Drop the current connection (if any). Useful after changing auth state —
// e.g. forcing a just-activated client to reconnect so it must pass the
// AUTH challenge instead of riding the previous auto-authed session.
void ble_disconnect();
