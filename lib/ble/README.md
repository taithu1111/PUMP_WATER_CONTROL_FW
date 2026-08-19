# lib/ble — BLE Transport Library

Thin wrapper around the ESP32 Arduino BLE stack (Nordic UART Service) with
built-in **LZSS compression**, a **channel-based dispatch** system, and an
**optional auth verifier** callback — so multiple independent features can
share one BLE connection with a single auth handshake.

---

## Wire packet format

```
┌──────┬──────────┬──────────┬────────────────────────────────┐
│ CH   │ LEN_L    │ LEN_H    │ PAYLOAD …                      │
│ 1 B  │ 1 B      │ 1 B      │ LEN bytes (compressed if on)   │
└──────┴──────────┴──────────┴────────────────────────────────┘
```

`ble_set_compress(false)` → payload is raw bytes, no LZSS.
Both sides (ESP32 and host) must agree on the same compression setting.

---

## Channel IDs  *(defined in `BLE.h`)*

| Constant        | Value | Direction        | Purpose                               |
|-----------------|-------|-----------------|---------------------------------------|
| `BLE_CH_AUTH`   | 0x00  | Host → ESP32    | Send raw password bytes to authenticate |
| `BLE_CH_OTA`    | 0x01  | Host → ESP32    | OTA firmware update blocks            |
| `BLE_CH_SENSOR` | 0x02  | Both            | Sensor readings (push) + text commands |

---

## Auth flow

```
Host                                     ESP32
──────                                   ──────
connect()
  ──────── BLE link up ────────────────►
                                         if _verifier == nullptr:
                                           _authed = true  (auto, no challenge)
                                         else:
  ◄──── AUTH_REQUIRED (CH 0x00) ─────── subscribe CCCD triggers this
  send CH=0x00, data=<password bytes>
  ─────────────────────────────────────►
                                         _verifier(data, len) called
                                           true  → AUTH_OK  + _connected=true
                                           false → AUTH_FAIL (link dropped at timeout)
  ◄──── AUTH_OK / AUTH_FAIL (CH 0x00) ──
```

> Timeout: if host does not send password within `timeout_ms` (default 10 000 ms)
> the ESP32 disconnects. Set via `ble_set_auth_verifier(fn, timeout_ms)`.

---

## Activation flow  *(app-level, uses `lib/auth`)*

```
Host (first use — device not yet activated)
────────────────────────────────────────────
connect()  →  auto-authed (no verifier set)

send CH=0x02  "ACTIVE::mypassword"
  ─────────────────────────────────────────►
                                             Auth::store("mypassword")
                                             g_activated = true
                                             ble_set_auth_verifier(Auth::verifyBytes)
  ◄──── "ACTIVE_OK" (CH 0x02) ──────────────
  (session continues without reconnect — already authed)

Host (subsequent connections)
──────────────────────────────
connect()  →  AUTH_REQUIRED
send CH=0x00  <password bytes>  →  AUTH_OK
(normal operation)

Host (factory reset)
─────────────────────
  (must be authenticated first)
send CH=0x02  "DEACTIVE"
  ─────────────────────────────────────────►
                                             Auth::clear()
                                             g_activated = false
                                             ble_set_auth_verifier(nullptr)
  ◄──── "DEACTIVE_OK" (CH 0x02) ────────────
```

---

## Normal operation flow

```
Host                                     ESP32
──────                                   ──────
connect()  →  AUTH_REQUIRED  →  send pw  →  AUTH_OK
                                         push sensor data every ~200 ms:
  ◄──── CH 0x02: "M:68.5\nT:28.1\nEC:420\npH:6.8\nN:3.2\nP:1.4\nK:2.1\n"
send CH=0x02  "OTA_REQUEST"  →  "OK"  →  device enters OTA mode
send CH=0x02  "LANG::EN"     →  "OK:LANG::EN"
send CH=0x02  "LANG::VN"     →  "OK:LANG::VN"
```

When no soil detected: `M:0.0`, `EC:0`, `N/P/K:0.0` — only `T` and `pH` are valid.

---

## OTA flow

```
Host                                     ESP32
──────                                   ──────
  (authenticated session)
send CH=0x02  "OTA_REQUEST"  ──────────►
                                         reply "OK", g_pendingOTA = true
                                         bleTask deletes sensorTask + oledTask
                                         starts Wi-Fi AP + OTA HTTP server
connect Wi-Fi AP, POST firmware
  ─────────────────────────────────────► flash + reboot
```

---

## Sleep / wake flow

```
ESP32 (no sensor data + no BLE peer for AUTO_SLEEP_TIMEOUT_MS = 10 min)
─────────────────────────────────────────────────────────────────────────
  → OLED: drawSleepEnter()
  → vTaskDelay 2 s
  → OLED dimmed, g_snap.valid = false
  → esp_light_sleep_start()  (timer wakeup every SLEEP_RETRY_INTERVAL_US = 30 s)

On each wake:
  soil.read() succeeds  →  exit sleep, resume normal display
  soil.read() fails     →  sleep again

BLE peer connects at any time → exit sleep immediately
```

---

## API

```cpp
// ── Setup ──────────────────────────────────────────────────────────────
void ble_begin(const char *name);
void ble_set_connect_cb(std::function<void(bool connected)> cb);
void ble_set_compress(bool enable);   // default true; must match host

// ── Channels ───────────────────────────────────────────────────────────
void ble_register_channel(uint8_t channel, BleRxHandler handler);
void ble_send(uint8_t channel, const uint8_t *data, size_t len);
void ble_dispatch();                  // call every ~1 ms from task

// ── Auth verifier (optional) ───────────────────────────────────────────
// Pass nullptr to disable auth (auto-connect). Pass a verifier to require
// the host to send the correct password on BLE_CH_AUTH within timeout_ms.
// Auth::verifyBytes (lib/auth) can be passed directly.
using BleAuthVerifier = std::function<bool(const uint8_t *data, size_t len)>;
void ble_set_auth_verifier(BleAuthVerifier verifier, uint32_t timeout_ms = 10000);

// ── Misc ───────────────────────────────────────────────────────────────
void ble_disconnect();
```

---

## LZSS compression

| Data type            | Typical ratio | Recommendation       |
|----------------------|---------------|----------------------|
| Firmware binary      | ~60–70 %      | Enable (OTA)         |
| Repeated / sparse    | ~10–15 %      | Enable               |
| Short text < 64 B    | ~110–120 %    | **Disable**          |
| Sensor readings      | ~110 %        | **Disable**          |

For this device: `ble_set_compress(false)` — all payloads are short ASCII text.

---

## Buffer limits

| Constant          | Default | Description                          |
|-------------------|---------|--------------------------------------|
| `BLE_RX_COMP_BUF` | 640 B   | Max compressed incoming packet       |
| `BLE_RX_DEC_BUF`  | 1024 B  | Max decompressed payload per packet  |
| `BLE_TX_COMP_BUF` | 640 B   | Compression output buffer (send)     |
| `BLE_RX_QUEUE_LEN`| 4096 B  | Byte FIFO from BLE ISR to ble_dispatch |
