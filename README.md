<div align="center">

<img src="assets/rotato-logo.png" alt="Rotato — Spin. Smash. Repeat." width="400"/>

# Rotato

### Spin. Smash. Repeat.

*A combat-style workshop robot built at [Asmbly](https://asmbly.org) — controlled from your browser, no app required.*

[![PlatformIO](https://img.shields.io/badge/built%20with-PlatformIO-orange?logo=platformio)](https://platformio.org)
[![ESP32-C3](https://img.shields.io/badge/MCU-ESP32--C3-blue?logo=espressif)](https://www.espressif.com/en/products/socs/esp32-c3)
[![License: Non-Commercial](https://img.shields.io/badge/license-Non--Commercial-red)](LICENSE)

**Author:** Angel Hernandez — [angel@thehomelab.dev](mailto:angel@thehomelab.dev) · [thehomelab.dev](https://www.thehomelab.dev)

🔗 **Project page:** [thehomelab.dev/projects/rotato](https://thehomelab.dev/projects/rotato)

</div>

---

> **AI Assistants (GitHub Copilot, Claude, etc.):** Read [`AI_CONTEXT.md`](AI_CONTEXT.md) instead — it contains the lean technical reference optimized for code generation.

---

## 1. What Is This Robot?

**Rotato** is a small combat-style workshop robot built at Asmbly. It has two drive wheels and an optional weapon motor, all controlled from your phone or laptop through a browser — no app to install.

The brain is an **ESP32-C3 Super Mini**, a tiny WiFi + Bluetooth microcontroller. The firmware (the software running on it) is written in C++ and built with PlatformIO.

---

## 2. How to Connect and Drive

### Step 1 — Power on the robot
Plug in the battery. You'll hear a startup beep (if the buzzer is installed).

### Step 2 — Connect to the robot's WiFi
The robot creates its own WiFi hotspot on boot:

| Setting | Value |
|---------|-------|
| SSID | `Rotato-XXXX` (XXXX = unique 4-char code for your robot) |
| Password | `12345678` |

Connect your phone or laptop to this network.

### Step 3 — Open the web controller
Open a browser and go to: **http://192.168.4.1**

You'll see the robot controller UI with a joystick, weapon button, and status panel.

### Connecting to your own WiFi (optional)
Open the Settings panel in the web UI, enter your WiFi network name and password, and save. The robot will reboot and join your network. If it can't connect, it falls back to its own hotspot automatically.

**To reset WiFi credentials:** Hold the BOOT button (small button on the ESP32 board) for 3 seconds after the robot has fully booted.

---

## 3. Hardware — What's Connected Where

### GPIO Pin Map

| GPIO | Connector | What's connected |
|------|-----------|-----------------|
| GPIO0 | — | Battery voltage sensor (ADC) |
| GPIO1 | J5 | Left wheel ESC signal |
| GPIO3 | J6 | Right wheel ESC signal |
| GPIO4 | J7 | Weapon motor ESC signal |
| GPIO5 | J8 | Safety switch (short to GND = weapon blocked) |
| GPIO6 | J9 | Expansion — free for your own additions |
| GPIO7 | JP1 | Buzzer (only active if JP1 solder jumper is closed) |
| GPIO9 | — | Built-in BOOT button (hold 3s to reset WiFi) |

### Battery
- Type: **2S LiPo** (two cells in series)
- Safe range: **6.0V** (empty) to **8.4V** (full)
- The battery voltage is measured through a resistor divider on GPIO0. The web UI shows battery % and voltage.

### Motors (ESC Signal)
All three motors use the standard RC servo signal: **50 Hz, pulse width 1000–2000 µs**.

| Pulse | Effect |
|-------|--------|
| 1000 µs | Full reverse (drive) / stopped (weapon) |
| 1500 µs | Neutral / stop |
| 2000 µs | Full forward / full speed |

### Safety Switch (J8)
A physical kill-switch for the weapon. When triggered, the weapon motor stops immediately regardless of what the web UI says. Wire a switch between GPIO5 and GND on connector J8.

### Buzzer (JP1)
A passive buzzer on GPIO7, connected through solder jumper JP1. Produces startup, weapon-on, weapon-off, and error tones. If JP1 is not soldered, GPIO7 is free to use for something else.

---

## 4. The Weapon Motor — Important Notes

The weapon ESC (motor speed controller) needs special handling:

1. **On power-on, the weapon is locked for 2 seconds.** The firmware holds the signal at minimum throttle (1000 µs) to arm the ESC. The weapon button in the UI is disabled until this completes.

2. **ESC full-range calibration** teaches the ESC what your signal range is. Trigger it from the Settings panel in the web UI. **Always disconnect the weapon motor (unplug the motor wires) before running calibration.** The ESC will beep to confirm each step.

3. **The safety switch physically overrides the weapon.** Even if the UI shows the weapon as "on", if the safety switch is triggered the weapon will not spin.

---

## 5. Building and Flashing the Firmware

### What You Need
- [VS Code](https://code.visualstudio.com/) with the [PlatformIO extension](https://platformio.org/install/ide?install=vscode)
- A USB-C cable connected to the ESP32-C3 Super Mini

### First-time Flash (important order!)

```bash
# 1. Upload the web UI to the robot's filesystem first
pio run --target uploadfs

# 2. Then flash the firmware
pio run --target upload
```

After that, for firmware-only changes you only need:
```bash
pio run --target upload
```

### Other Useful Commands

```bash
# Build without flashing (check for errors)
pio run

# Open the serial monitor to see debug output
pio device monitor
```

### Viewing Debug Output
The robot prints detailed information over USB Serial at **115200 baud**. Open the serial monitor right after powering on to see the boot sequence, IP address, and any errors.

---

## 6. The One File You Need to Edit: `src/config.h`

Almost all hardware customization lives in a single file: **`src/config.h`**. It is heavily commented. Here are the most common things to change:

### Motor spins the wrong way
```cpp
#define MOTOR_LEFT_REVERSED   false   // change to true
#define MOTOR_RIGHT_REVERSED  true    // change to false
```

### Change the robot's default WiFi name or password
```cpp
#define AP_SSID_PREFIX  "Rotato-"   // your robot will be "Rotato-XXXX"
#define AP_PASSWORD     "12345678"  // must be 8+ characters
```

### Disable the buzzer (if JP1 is not soldered)
```cpp
// Comment out this line:
// #define HAS_BUZZER
```

### Disable the safety switch (if none is connected)
```cpp
// Comment out this line:
// #define HAS_SAFETY_SWITCH
```

### Adjust drive range (if motors only move a little)
```cpp
#define PWM_DRIVE_HALF_RANGE_US  500  // try increasing to 600 or 700
```

---

## 7. Files You Should Know About

| File | What it does | Should you edit it? |
|------|-------------|---------------------|
| `src/config.h` | All hardware settings | ✅ Yes — start here |
| `src/version.h` | Firmware version number | ✅ Yes — bump when releasing |
| `src/main.cpp` | Wires everything together | ✅ Yes — for new features |
| `src/hardware/*.h/.cpp` | Motor, buzzer, battery, safety drivers | ✅ Yes |
| `src/connectivity/*.h/.cpp` | WiFi, web server, BLE | ✅ Yes |
| `data/index.html` | The web UI running in your browser | ✅ Yes |
| `src/version_build.h` | Auto-generated — build timestamp | ❌ No |
| `src/web_ui_html.h` | Auto-generated — embedded web UI | ❌ No |
| `build_number.txt` | Auto-incremented build counter | ❌ No |
| `scripts/*.py` | Build pipeline scripts | ⚠️ Advanced only |

---

## 8. Common Tasks Step by Step

### Give your robot a custom name
Web UI → Settings → Robot Name → type a name → Save. The robot reboots and uses the new name as its WiFi SSID.

### Calibrate the battery reading
If the battery percentage looks wrong, go to the web UI Settings → Battery Calibration. Adjust the ratio until the displayed voltage matches a multimeter reading.

### Add a new sound effect
1. Open `src/hardware/BuzzerController.cpp`
2. Define a new sequence array: `static const BeepStep mySound[] = {{800,100},{0,50},{1200,200}};`
3. Add `void beepMyEvent() { playSequence(mySound, 3); }` to the class
4. Declare it in `BuzzerController.h`
5. Call `buzzer.beepMyEvent()` from `main.cpp`

### Add a new button/control to the web UI
1. Add the HTML/JS to `data/index.html`
2. Send a new JSON message over WebSocket, e.g. `{"type":"myCommand","value":42}`
3. Add a callback type and `onMyCommand()` registration method in `WebServerManager.h`
4. Parse the new type in `WebServerManager.cpp`
5. Register and handle the callback in `main.cpp`

---

> **AI Assistants:** See [`AI_CONTEXT.md`](AI_CONTEXT.md) for the technical reference optimized for code generation.

---

## License

Copyright (c) 2026 **Angel Hernandez** — [www.thehomelab.dev](https://www.thehomelab.dev)

This project is released under a **Non-Commercial Source-Available License**. You are free to use, study, modify, and share it for non-commercial purposes, subject to these rules:

- **Attribution required** — Any project (code, firmware, article, video, workshop, or AI-generated output) that uses or is derived from this software must credit: `Angel Hernandez — www.thehomelab.dev`. This applies even if you only used `AI_CONTEXT.md` as a prompt.
- **No commercial use** — You may not sell, license, or use this software (or derivatives) to generate revenue without explicit written permission from the author.
- **Share-alike** — Derivatives must carry the same license.

See [LICENSE](LICENSE) for the full legal text. For commercial licensing: [angel@thehomelab.dev](mailto:angel@thehomelab.dev)

