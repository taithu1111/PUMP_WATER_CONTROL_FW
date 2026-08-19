# Operating mode switch

## Wiring

The mode selector uses ESP32-S3 GPIO 6 with the internal pull-up enabled.

| GPIO 6 | Switch state | Operating mode |
| --- | --- | --- |
| LOW | Closed to GND | Online |
| HIGH | Open | Offline manual |

Do not connect GPIO 6 to an external voltage. Connect the switch only between
GPIO 6 and GND. The firmware accepts a change after the signal remains stable
for 200 ms.

## Source of truth and persistence

The physical GPIO is always the source of truth. NVS stores only the last
stable mode under namespace `operating_mode`, key `last_mode`; the stored value
never overrides the switch at boot. NVS is written only when the stable mode
differs from the last stored value or the key does not exist.

## Online mode

- Stored Wi-Fi credentials: start Wi-Fi and MQTT; BLE remains off.
- No stored Wi-Fi credentials: start BLE provisioning.
- Relay timeout and interval schedule automation runs normally.
- Returning from Offline reconciles timeout and schedule state against the
  current epoch before automation changes a relay.

## Offline manual mode

- Stop BLE provisioning, MQTT and Wi-Fi.
- Pause relay timeout and interval schedule execution.
- Keep all Wi-Fi, timeout and schedule data in NVS.
- Keep the current physical relay states unchanged when entering Offline.
- Physical/manual relay control remains separate from the mode selector.

## Expected debug logs

Online selection:

```text
[mode] GPIO stable LOW -> ONLINE
[main] Operating mode selected: ONLINE
[automation] Resumed
```

Offline selection:

```text
[mode] GPIO stable HIGH -> OFFLINE
[main] Operating mode selected: OFFLINE
[main] Online services stopped
```

When Offline is selected immediately after boot, automation is already paused,
so an additional `[automation] Paused` line is not required. A runtime
Online-to-Offline transition does print that line.

## Hardware test checklist

1. Boot with GPIO 6 connected to GND and verify Online logs and MQTT status.
2. Boot with GPIO 6 open and verify Offline logs, no MQTT connection and no BLE
   advertising.
3. While running, open GPIO 6 and verify MQTT disconnects after debounce without
   changing relay states.
4. Connect GPIO 6 to GND and verify stored credentials reconnect Wi-Fi/MQTT; if
   credentials are absent, verify BLE advertising starts.
5. Toggle the switch rapidly for less than 200 ms and verify no mode transition.
6. Reboot in both switch positions and verify GPIO, not the stored NVS value,
   selects the mode.
7. Start a timeout, enter Offline, wait across its expiry, then return Online and
   verify `onExpire` is applied.
8. Enter Offline across an interval `startAt` or `endAt`, return Online and verify
   the schedule becomes Active, Completed or Missed according to current time.

The full four-relay interval `startAt`/`endAt` test remains a separate pending
test and is not considered passed by the operating-mode tests above.
