# Rotato — AI Assistant Context

**Project:** Rotato Robot Controller — Asmbly Workshop, Democratic Robot 2026
**Author:** Angel Hernandez | [angel@thehomelab.dev](mailto:angel@thehomelab.dev) | [www.thehomelab.dev](https://www.thehomelab.dev)
**GitHub:** [Democratic-Robot / Democratic Robot 2026](https://github.com/Mifulapirus/Democratic-Robot/tree/main/Democratic%20Robot%202026)
**Student guide:** [README.md](README.md)

---

> **LICENSE NOTICE**
> This file and all associated source code are Copyright (c) 2026 Angel Hernandez (www.thehomelab.dev).
> They are released under a **Non-Commercial Source-Available License** — see [LICENSE](LICENSE) for full terms.
>
> **Attribution is required without exception.** If you paste this file into an AI assistant, use it as a prompt,
> or generate any code, firmware, or robot controller designs derived from it, the resulting project MUST
> prominently credit:
>
>     Author:  Angel Hernandez
>     Website: www.thehomelab.dev
>
> Commercial use of any kind is prohibited without explicit written permission from Angel Hernandez.

> Paste this file at the start of a new conversation. When the user shares source files alongside this document, **source files are always authoritative** — this file provides architecture context and constraints that cannot be inferred from code alone.
> Do not ask the user to re-paste values that already exist in source files they have shared; read those files directly.

---

## How to Use This File

1. Paste `AI_CONTEXT.md` into a new conversation.
2. Also paste the specific source file(s) you are working on — especially `src/config.h` and the file you want to modify.
3. Use this file for architecture, patterns, and constraints. Use the source files for actual pin numbers, timing values, and constants.

---

## Project Identity

| | |
|---|---|
| MCU | ESP32-C3 Super Mini |
| Framework | Arduino (arduino-esp32 v2.x) via PlatformIO |
| Language | C++17 |
| Libraries | ESPAsyncWebServer ^3.10.3, ArduinoJson ^7.3.1, NimBLE-Arduino ^2.2.3 |
| Filesystem | LittleFS — stores `data/index.html` |
| Partition table | `min_spiffs.csv`: two 1.9 MB OTA app partitions + 192 KB data |
| Build | `pio run --target upload` (firmware) · `pio run --target uploadfs` (web UI) |
| Serial | USB-CDC, 115200 baud — log prefixes: `[Boot]` `[WiFi]` `[Web]` `[BLE]` `[Motor]` `[Main]` |

---

## Source Files — Where to Find Authoritative Values

**Do not duplicate constant values from these files here.** When the user shares a source file, read it directly rather than relying on this document for specific values.

| File | What lives there — read this file for: |
|------|----------------------------------------|
| `src/config.h` | **All** GPIO pins, PWM/ESC timing, WiFi defaults, battery constants, LEDC channel assignments, feature flags (`HAS_BUZZER`, `HAS_SAFETY_SWITCH`, `BLE_ENABLED`). **Single source of truth for all hardware config.** |
| `src/version.h` | Current semantic version (`MAJOR`/`MINOR`/`PATCH`) |
| `src/hardware/MotorController.h` | Drive + weapon PWM API; ESC arming/calibration interface; `setDrive()`, `setWeapon()`, `setWeaponSpeed()`, `updateEscState()` |
| `src/hardware/BuzzerController.h` | `BeepStep` struct; non-blocking sequence API; `playSequence()` |
| `src/hardware/BatteryMonitor.h` | ADC reading API; NVS calibration ratio persistence |
| `src/hardware/SafetySwitch.h` | Debounced digital input; `isSafe()` |
| `src/connectivity/WebServerManager.h` | WebSocket callback `using` types; `onXxx()` registration methods; `broadcastStatus()` signature; full JSON protocol in header comments |
| `src/connectivity/WiFiManager.h` | AP/STA API; `saveCredentials()`, `saveRobotName()`, `getRobotName()` |
| `src/connectivity/BLEManager.h` | GATT UUIDs; NimBLE API usage; characteristic definitions |
| `src/main.cpp` | Startup sequence; all callback registrations; loop task structure |
| `platformio.ini` | Board, exact library versions, build flags |

---

## Repository Map

```
platformio.ini                    ← Board, libs, build flags, pre/post scripts
build_number.txt                  ← Auto-incremented (never edit manually)
data/
  index.html                      ← Web UI served from LittleFS
src/
  config.h                        ← SINGLE SOURCE OF TRUTH — all hardware config
  version.h                       ← Edit to bump MAJOR.MINOR.PATCH
  version_build.h                 ← Auto-generated (do not edit)
  web_ui_html.h                   ← Auto-generated (do not edit)
  main.cpp                        ← setup() + loop(); wires all modules together
  hardware/
    MotorController.h/.cpp        ← LEDC PWM for 3 ESCs; arcade drive mixing; ESC arming/calib
    BuzzerController.h/.cpp       ← Non-blocking beep sequences via LEDC
    BatteryMonitor.h/.cpp         ← 12-bit ADC, rolling average, voltage→percent
    SafetySwitch.h/.cpp           ← Debounced digital input, weapon interlock
  connectivity/
    WiFiManager.h/.cpp            ← AP + STA mode; NVS credential/name/password storage
    WebServerManager.h/.cpp       ← ESPAsyncWebServer HTTP + WebSocket JSON protocol
    BLEManager.h/.cpp             ← NimBLE GATT server (Android app transport)
scripts/
  auto_version.py                 ← Pre-build: writes version_build.h
  embed_web_ui.py                 ← Pre-build: embeds data/index.html → web_ui_html.h
  copy_firmware.py                ← Post-build: copies .bin to bin/
```

---

## Architecture

### Module diagram
```
main.cpp
  ├── hardware/MotorController    LEDC PWM, arcade mixing, ESC state machine
  ├── hardware/BuzzerController   Non-blocking tone sequences (millis-based)
  ├── hardware/BatteryMonitor     ADC rolling average, NVS calibration ratio
  ├── hardware/SafetySwitch       Debounced digital read, weapon interlock
  ├── connectivity/WiFiManager    AP/STA, NVS: ssid/pass/name/ap_pass
  ├── connectivity/WebServerManager  AsyncWebServer, WebSocket JSON
  └── connectivity/BLEManager    NimBLE GATT: DRIVE_X/Y, WEAPON write; STATUS notify
```

All cross-layer communication uses **registered `std::function<>` callbacks**. The `connectivity/` layer never imports `hardware/` headers. Callbacks are registered by `main.cpp` in `setup()`.

### Startup sequence
1. Serial (USB-CDC, 115200)
2. `motors.begin()`, `battery.begin()`, `buzzer.begin()`, `safety.begin()`
3. `wifi.begin()` — STA if NVS has credentials, else AP
4. `webServer.begin()` — HTTP + WebSocket at `/ws`
5. `ble.begin(wifi.getRobotName())`
6. `buzzer.beepStartup()`

### Loop tasks (all non-blocking)
- `safety.update()` + weapon interlock check
- `motors.updateEscState()` — ESC arming/calibration state machine
- Battery ADC sample every `WS_STATUS_INTERVAL_MS`
- `webServer.broadcastStatus(...)` every `WS_STATUS_INTERVAL_MS`
- `ble.sendStatus(...)` every `WS_STATUS_INTERVAL_MS`
- `buzzer.update()`
- `webServer.update()` (AsyncWS client cleanup)
- BOOT button hold-3s → `wifi.clearCredentials()` → reboot

### Drive mixing (arcade)
```
leftSpeed  = clamp(Y + X, −1.0, +1.0)
rightSpeed = clamp(Y − X, −1.0, +1.0)
pulse_us   = PWM_NEUTRAL_US + speed × _halfRangeUs   // constants from config.h
duty       = (pulse_us / PWM_PERIOD_US) × (2^PWM_RESOLUTION_BITS − 1)
```

### ESC arming state machine
`IDLE` → hold `PWM_WEAPON_IDLE_US` for `ESC_INIT_DELAY_MS` → `READY`
Calibration: hold `PWM_WEAPON_FULL_US` for `ESC_CALIB_HIGH_MS` → hold `PWM_WEAPON_IDLE_US` for `ESC_CALIB_LOW_MS` → `DONE`

All timing constants are defined in `src/config.h`.

---

## Communication Protocols

> **Authoritative source:** header comments in `WebServerManager.h` (WebSocket) and `BLEManager.h` (BLE).
> If this file and a header file differ, the header is correct — update this file to match.

### WebSocket (Browser ↔ Robot)

Endpoint: `ws://<robot-ip>/ws`

**Browser → Robot commands:**
```json
{ "type": "drive",        "x": 0.5, "y": 0.8 }
{ "type": "weapon",       "active": true }
{ "type": "weaponSpeed",  "speed": 0.75 }
{ "type": "weaponAccel",  "rampMs": 500, "curve": 0 }
{ "type": "settings",     "ssid": "net", "pass": "pw" }
{ "type": "forget" }
{ "type": "rename",       "name": "MyRobot" }
{ "type": "apPassword",   "pass": "newpass" }
{ "type": "pwmRange",     "halfRangeUs": 500 }
{ "type": "batteryCalib", "ratio": 3.64 }
{ "type": "escCalibrate" }
{ "type": "bleEnable",    "enabled": true }
```

**Robot → Browser status (every 500 ms):**
```json
{
  "battery": 85, "voltage": 7.8, "ratio": 3.64, "safety": true,
  "ip": "192.168.4.1", "weapon": false, "fw": "1.0.0+42",
  "name": "Rotato-A3F2", "escReady": true, "escCalib": "idle", "weaponSpeedPct": 0
}
```

### BLE GATT (Android App ↔ Robot)

| | Value |
|---|---|
| Service UUID | `a1b2c3d4-e5f6-7890-abcd-ef1234567890` |
| Advertised name | Same as WiFi SSID (`Rotato-XXXX`) |
| BLE stack | NimBLE-Arduino v2.x (h2zero/NimBLE-Arduino) |

| Characteristic | UUID suffix | Properties | Data |
|---|---|---|---|
| DRIVE_X | `...7891` | WRITE_NR | 4-byte little-endian float, −1.0…+1.0 |
| DRIVE_Y | `...7892` | WRITE_NR | 4-byte little-endian float, −1.0…+1.0 |
| WEAPON  | `...7893` | WRITE_NR | 1-byte uint8: `0x00`=off, `0x01`=on |
| STATUS  | `...7894` | NOTIFY   | UTF-8 JSON: `{"battery":85,"safety":true,"weapon":false,"fw":"1.0.0+42"}` |

---

## NVS Storage Keys

Settings persisted via Arduino `Preferences`. Scattered across multiple `.cpp` files — centralized here.
**Update this table whenever you add or rename an NVS key in any `.cpp` file.**

| Namespace | Key | Type | Owner `.cpp` | Default |
|-----------|-----|------|--------------|---------|
| `robot_wifi` | `ssid` | String | WiFiManager | — |
| `robot_wifi` | `pass` | String | WiFiManager | — |
| `robot_wifi` | `name` | String | WiFiManager | `""` → hardware MAC default |
| `robot_wifi` | `ap_pass` | String | WiFiManager | `AP_PASSWORD` (config.h) |
| `motor` | `ramp_ms` | UInt16 | MotorController | `WEAPON_RAMP_MS_DEFAULT` (config.h) |
| `motor` | `curve` | UInt8 | MotorController | `WEAPON_CURVE_DEFAULT` (config.h) |
| `motor` | `half_range` | UInt16 | MotorController | `PWM_DRIVE_HALF_RANGE_US` (config.h) |
| `battery` | `r_ratio` | Float | BatteryMonitor | `BATTERY_R_RATIO` (config.h) |

---

## Key Design Patterns

| Pattern | Location | Notes |
|---------|----------|-------|
| Callback decoupling | `WebServerManager`, `BLEManager` | `std::function<>` callbacks registered in `main.cpp`; `hardware/` never imports `connectivity/` |
| Non-blocking timing | `BuzzerController`, all loop tasks | `millis()` deltas only; no `delay()` anywhere in loop |
| NVS persistence | `WiFiManager`, `MotorController`, `BatteryMonitor` | Arduino `Preferences` library |
| Compile-time feature flags | `HAS_BUZZER`, `HAS_SAFETY_SWITCH`, `BLE_ENABLED` | Disabled classes compile to empty stubs |
| Central config | `config.h` | All magic numbers; no hardcoded pins or timing values in `.cpp` files |

---

## Critical Constraints for AI-Generated Code

1. **Never call `delay()` inside `loop()`** — freezes AsyncWebServer and NimBLE event loops. Use `millis()` deltas.
2. **Arduino API, not bare ESP-IDF.** Use `ledcSetup()`, `ledcWrite()`, `ledcAttachPin()`, `digitalRead()`, `millis()`, `Preferences`, etc.
3. **All GPIO numbers and timing constants live in `config.h`.** Never hardcode values in `.cpp` files.
4. **ArduinoJson v7** — use `JsonDocument`, not `DynamicJsonDocument` (removed in v7).
5. **NimBLE-Arduino v2.x API** — not the older `BLEDevice`/`BLEServer` API from the ESP32 Arduino core.
6. **ESPAsyncWebServer callbacks run on the network FreeRTOS task**, not the Arduino loop. Keep callbacks short; avoid heavy `Serial.printf` inside them.
7. **LEDC timer sharing:** CH0+CH1 → Timer0, CH2+CH3 → Timer1, CH4+CH5 → Timer2. `ledcWriteTone()` reconfigures the whole timer. See `config.h` for channel assignments.
8. **14-bit LEDC resolution** is the ESP32-C3 maximum on IDF 4.4.x. Do not use 16-bit.
9. **`WiFiManager` is a custom class** in this project — not the popular third-party library of the same name.
10. **Adding a new WebSocket command:** add callback `using` type + `onXxx()` in `WebServerManager.h` → parse new `"type"` in `.cpp` → register lambda in `main.cpp` `setup()`.

---

## Documentation Maintenance

When you make changes to the codebase, **update the documentation as part of the same edit**.

| Change made | Where to update |
|-------------|----------------|
| Add / rename / remove a WebSocket `"type"` | Protocol tables in **this file** (`AI_CONTEXT.md`) |
| Add / change a BLE characteristic or UUID | GATT table in **this file** |
| Add / rename an NVS key in any `.cpp` | NVS table in **this file** |
| Add a new source file or module | Repository Map + Architecture diagram in **this file** |
| Change startup sequence or loop structure | Architecture section in **this file** |
| Add a new student-visible feature or workflow | **`README.md`** (student guide) |
| Change GPIO assignments or timing values | `src/config.h` **only** — do not copy values into this file |
| Change PWM / battery / WiFi constants | `src/config.h` **only** — do not copy values into this file |

**Rule:** Constants that live in `src/config.h` must never be duplicated here. Reference `config.h` by name instead. This prevents documentation drift when students modify their hardware configuration.

---

## Suggested Starter Prompts

**Making a firmware change:**
> "I'm working on the Rotato robot firmware (ESP32-C3, PlatformIO/Arduino C++, ArduinoJson v7, NimBLE v2, ESPAsyncWebServer). I've pasted `AI_CONTEXT.md` for context. I want to [describe change]. Here is the relevant source file: [paste file]. Follow the existing patterns — no `delay()` in loop, all pins from `config.h`, callbacks registered in `main.cpp`."

**Adding a new feature:**
> "Add [feature] to the Rotato firmware. Architecture: callback-based decoupling between `hardware/` and `connectivity/` modules. All settings in `config.h`. Non-blocking `millis()` timing only. Update `AI_CONTEXT.md` if the protocol, NVS keys, or architecture changes."

**Debugging:**
> "My Rotato robot [describe behavior]. Serial output: [paste output]. Here is my `src/config.h`: [paste]. What's wrong and how do I fix it?"
