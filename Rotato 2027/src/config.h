#pragma once
// =============================================================================
// config.h — Robot Controller Configuration
// =============================================================================
// Copyright (c) 2026 Angel Hernandez (www.thehomelab.dev)
// Non-Commercial Source-Available License — see LICENSE file for full terms.
// Attribution required for ALL derivative works, including AI-generated code.
// Commercial use prohibited without explicit written permission.
//
// This is the ONE FILE you need to change when adjusting hardware settings.
// All pin numbers, PWM timing values, WiFi credentials, and feature switches
// live here so students only have to look in one place.
//
// HOW TO USE:
//   - Change GPIO numbers if you wire things differently
//   - Drive motors run from the on-board AT8236 dual H-bridge (U1).
//   - Weapon is still an RC ESC on the 3-pin servo header (M4).
//   - Change AP_SSID / AP_PASSWORD for your robot's WiFi name
// =============================================================================

// ── Feature Flags ─────────────────────────────────────────────────────────────
//
// HAS_BUZZER        — passive buzzer BZ1 wired directly to GPIO5.
// HAS_SAFETY_SWITCH — the Rotato 2027 board has NO safety-switch input
//                     (SW1/SW2 switch battery power instead). Leave undefined
//                     unless you wire a kill-switch to a free GPIO and set
//                     PIN_SAFETY_SWITCH below.
//
#define HAS_BUZZER
// #define HAS_SAFETY_SWITCH

// Enable/disable BLE (NimBLE-Arduino — connects the Android app to the robot)
#define BLE_ENABLED

// Bluetooth game controller (Xbox Wireless Controller over BLE). See the
// "Game Controller" section below for the button mapping and tuning.
#define GAMEPAD_ENABLED

// Blue LED on the D1 Mini module itself (GPIO2). The red LED next to it is
// hard-wired to 3.3V (power indicator) and can't be controlled.
// See the "On-board status LED" section below for what the blink patterns mean.
#define HAS_ONBOARD_LED

// ── Pin Definitions ──────────────────────────────────────────────────────────
//
// Rotato 2027 board — ESP32 D1 Mini (U2, ESP32-WROOM-32) + AT8236 (U1).
// Taken from Electronics/rotato 2027/rotato 2027.kicad_sch.
//
//   Signal              GPIO   D1 Mini label   Board net / part
//   ──────────────────  ────   ─────────────   ──────────────────────────
//   Battery sense        36    SVP / A0        R1 47K / R2 10K divider
//   Left motor  AIN1     26    D0              U1 AIN1 → AOUT1 → M2
//   Left motor  AIN2     18    D5              U1 AIN2 → AOUT2 → M2
//   Right motor BIN1     19    D6              U1 BIN1 → BOUT1 → M3
//   Right motor BIN2     23    D7              U1 BIN2 → BOUT2 → M3
//   Weapon ESC signal    17    D3              M4 servo header pin 1
//   Buzzer                5    D8              BZ1 (+)
//   RGB status LED       16    D4              D2 DIN (via R3 330Ω)
//   I2C SDA              21    D2              J3 pin 4
//   I2C SCL              22    D1              J3 pin 3
//   BOOT button           0    —               on the D1 Mini module (if fitted)
//   On-board blue LED     2    —               on the D1 Mini module (status)
//
#define PIN_BATTERY_ADC   36  // GPIO36 (ADC1_CH0) — input-only, no pull-ups
#define PIN_LEFT_IN1      26  // AT8236 AIN1
#define PIN_LEFT_IN2      18  // AT8236 AIN2
#define PIN_RIGHT_IN1     19  // AT8236 BIN1
#define PIN_RIGHT_IN2     23  // AT8236 BIN2
#define PIN_MOTOR_WEAPON  17  // Weapon ESC signal (M4)
#define PIN_BUZZER         5  // Passive buzzer BZ1 (GPIO5 is a strapping pin — fine as output)
#define PIN_STATUS_LED    16  // Addressable RGB LED D2 (WS2812-style, 1 pixel) — not used yet
#define PIN_I2C_SDA       21  // J3 expansion connector — not used yet
#define PIN_I2C_SCL       22  // J3 expansion connector — not used yet
#define PIN_BOOT_BUTTON    0  // D1 Mini BOOT button (active LOW, hold 3s to clear WiFi)
#define PIN_ONBOARD_LED    2  // D1 Mini blue LED (GPIO2 is a strapping pin — fine as output after boot)
#define ONBOARD_LED_ACTIVE_HIGH true  // true = writing HIGH turns the LED on
// #define PIN_SAFETY_SWITCH  ?  // No safety switch on this board (see HAS_SAFETY_SWITCH)

// ── Drive motors — AT8236 H-bridge PWM ────────────────────────────────────────
//
// The AT8236 has two inputs per motor (xIN1, xIN2):
//   IN1  IN2   Result
//    0    0    Coast (outputs off)
//    1    0    Forward
//    0    1    Reverse
//    1    1    Brake
//
// Speed is set by PWM on the inputs. Two decay modes are supported:
//   Slow decay (DRIVE_SLOW_DECAY = true, recommended):
//     forward = IN1 high, IN2 PWM (inverted). Motor brakes during the off-time,
//     giving a more linear speed response and better low-speed control.
//     Stopped = both high = brake.
//   Fast decay (DRIVE_SLOW_DECAY = false):
//     forward = IN1 PWM, IN2 low. Motor coasts during the off-time.
//     Stopped = both low = coast.
//
// 20 kHz keeps the PWM above human hearing (no motor whine).
// At 20 kHz the LEDC clock (80 MHz) allows up to 11 bits; 10 bits is plenty.
//
#define DRIVE_PWM_FREQ_HZ       20000
#define DRIVE_PWM_BITS          10      // duty 0–1023
#define DRIVE_SLOW_DECAY        true
#define DRIVE_MAX_POWER         100     // % of full duty at full stick (1–100).
                                        // Lower it if the drive motors are rated below
                                        // battery voltage, e.g. 6V motors on 3S → ~50.
                                        // (For beginners, use the UI's Speed Limit instead.)

// ── Weapon — RC ESC Timing ────────────────────────────────────────────────────
//
// Standard RC/ESC protocol: 50Hz signal, pulse width 1000–2000 microseconds.
//
// The ESP32 LEDC peripheral generates these pulses.
// Timer resolution is 14-bit: values 0–16383 represent 0–20ms (one full period).
//   Duty formula: duty = (pulse_us / 20000.0) * ((1 << PWM_RESOLUTION_BITS) - 1)
//
#define PWM_FREQUENCY_HZ    50      // 50 Hz = 20ms period (standard RC protocol)
#define PWM_RESOLUTION_BITS 14      // 14-bit resolution
#define PWM_PERIOD_US       20000   // 20,000 µs = one full period
#define PWM_WEAPON_IDLE_US  1000    // Weapon off (minimum pulse = ESC disarmed)
#define PWM_WEAPON_FULL_US  2000    // Weapon full speed

// ── Weapon ESC Initialization & Calibration ───────────────────────────────────
//
// RC ESCs require the PWM signal to sit at minimum throttle (PWM_WEAPON_IDLE_US)
// at power-on for at least ESC_INIT_DELAY_MS before they will accept commands.
// This mimics an RC transmitter joystick being held all the way down.
// The weapon button is disabled in firmware and UI until this hold completes.
//
// Full-range calibration (user-triggered) teaches the ESC the signal endpoints:
//   Step 1 — Hold maximum throttle (PWM_WEAPON_FULL_US) for ESC_CALIB_HIGH_MS
//   Step 2 — Hold minimum throttle (PWM_WEAPON_IDLE_US) for ESC_CALIB_LOW_MS
// Most ESCs beep to confirm each step. Always disconnect the motor first.
//
#define ESC_INIT_DELAY_MS    2000   // Hold minimum pulse at power-on (ms)
#define ESC_CALIB_HIGH_MS    3000   // Full-range calibration: hold max throttle (ms)
#define ESC_CALIB_LOW_MS     3000   // Full-range calibration: hold min throttle (ms)

// ── Weapon speed & acceleration ───────────────────────────────────────────────
//
// Weapon speed is 0.0 (off) → 1.0 (full), mapped to PWM_WEAPON_IDLE_US–FULL_US.
// The firmware ramps the PWM output at a configurable rate so the motor
// accelerates/decelerates smoothly instead of jumping immediately.
//
// WEAPON_RAMP_MS_DEFAULT: time (ms) to sweep 0→100%. 0 = instant.
// WEAPON_CURVE_DEFAULT  : output shaping applied after the ramp:
//   0 = Linear    — constant step size throughout the sweep
//   1 = Quadratic — x² : slow start, fast finish  (protects gear/belt at start)
//   2 = S-Curve   — smoothstep: eases in AND out (gentlest for chain/belt drives)
//
#define WEAPON_RAMP_MS_DEFAULT   500   // ms for full 0→100% sweep
#define WEAPON_CURVE_DEFAULT       0   // 0=linear, 1=quadratic, 2=s-curve

// LEDC channels (ESP32 has 16: 0–7 high-speed, 8–15 low-speed)
// IMPORTANT — channel-to-timer mapping (arduino-esp32 v2.x, high-speed group):
//   CH 0, 1 → Timer 0   CH 2, 3 → Timer 1   CH 4, 5 → Timer 2   CH 6, 7 → Timer 3
// Channels sharing a timer share its frequency, and ledcWriteTone() reconfigures
// the ENTIRE timer, so each frequency group gets its own timer(s):
//   Left  AIN1/AIN2 : CH 0, 1 → Timer 0  ┐ 20 kHz drive
//   Right BIN1/BIN2 : CH 2, 3 → Timer 1  ┘
//   Weapon ESC      : CH 4    → Timer 2  — 50 Hz, isolated
//   Buzzer          : CH 6    → Timer 3  — tone, isolated
#define LEDC_CH_LEFT_IN1      0
#define LEDC_CH_LEFT_IN2      1
#define LEDC_CH_RIGHT_IN1     2
#define LEDC_CH_RIGHT_IN2     3
#define LEDC_CH_WEAPON_MOTOR  4
#define LEDC_CH_BUZZER        6

// Motor direction flags — set to true if a motor spins the wrong way
// (Common in differential drive: one motor is mounted mirrored)
#define MOTOR_LEFT_REVERSED   false
#define MOTOR_RIGHT_REVERSED  true

// ── Battery Monitor ───────────────────────────────────────────────────────────
//
// Supports 2S (7.4V) and 3S (11.1V) LiPo packs. Battery type is selectable
// at runtime via the web UI and persisted to NVS.
// Both share the same voltage divider: R1=47KΩ (top), R2=10KΩ (bottom), C2 100nF
//   V_adc = V_batt × R2 / (R1 + R2) = V_batt × 10K/57K
//   V_batt = V_adc × 5.7
//
// ADC headroom check (11dB attenuation, linear up to ~2.5V on ESP32):
//   3S full charge → 12.6 / 5.7 = 2.21V  ✓
//   2S full charge →  8.4 / 5.7 = 1.47V  ✓
//
// The ADC is read with analogReadMilliVolts(), which applies the chip's
// factory (eFuse) calibration, so no reference-voltage constant is needed.
// Fine-tune BATTERY_R_RATIO at runtime from the web UI if your meter disagrees.
//
#define BATTERY_R_RATIO        5.7f  // Voltage divider multiplier (R1=47K, R2=10K)

// Voltage thresholds for each battery type (100% and 0% endpoints):
#define BATTERY_VOLTAGE_FULL_2S   8.4f  // 100% — 2S LiPo (4.2V × 2)
#define BATTERY_VOLTAGE_EMPTY_2S  6.0f  //   0% — 2S LiPo minimum safe (3.0V × 2)
#define BATTERY_VOLTAGE_FULL_3S  12.6f  // 100% — 3S LiPo (4.2V × 3)
#define BATTERY_VOLTAGE_EMPTY_3S  9.0f  //   0% — 3S LiPo minimum safe (3.0V × 3)
#define BATTERY_DEFAULT_CELLS     3     // Default: 3 = 3S. Set to 2 for 2S.

#define BATTERY_NUM_SAMPLES   8      // Number of samples for rolling average

// ── WiFi / Access Point ───────────────────────────────────────────────────────
//
// The robot creates its own WiFi hotspot by default.
// Connect your phone/laptop to this network, then open 192.168.4.1 in a browser.
//
// The last 4 characters of the robot's MAC address are appended to AP_SSID
// automatically, so multiple robots in the same room won't conflict.
//
#define AP_SSID_PREFIX    "Rotato-"   // Final SSID will be e.g. "Rotato-A3F2"
#define AP_PASSWORD       "12345678"  // Minimum 8 characters for WPA2
#define AP_IP_ADDRESS     "192.168.4.1" // Default ESP32 AP gateway address
#define STA_CONNECT_TIMEOUT_MS 10000   // 10 seconds to try connecting to home WiFi

// ── Web Server ────────────────────────────────────────────────────────────────
#define WEB_SERVER_PORT       80    // Standard HTTP port
#define WS_STATUS_INTERVAL_MS 500  // How often to send status to browser (ms)

// ── On-board status LED (D1 Mini blue LED, GPIO2) ─────────────────────────────
//
// The LED shows one pattern at a time; the highest-priority state wins:
//
//   Priority  State                          Pattern
//   ────────  ─────────────────────────────  ──────────────────────────────────
//   1         Weapon spinning                Fast strobe (10 flashes/s)
//   2         ESC arming (first 2 s of boot) Rapid blink (5 flashes/s)
//   3         Battery low (or no battery)    Double blink every second
//   4         Controller pairing window open Slow blink (1 flash/s)
//   5         Someone connected              Solid ON  (browser, BLE app or gamepad)
//   6         Idle, nobody connected         Short "heartbeat" blip every 2 s
//
// The heartbeat also proves the firmware is running: a LED that is stuck
// fully on or fully off with nobody connected means the firmware has hung.
//
#define STATUS_LED_LOW_BATT_PCT   15   // battery % at or below which "battery low" shows

// ── Game Controller (Xbox Wireless Controller over Bluetooth LE) ──────────────
//
// Supported: Xbox Wireless Controller (Series X|S model 1914, and Xbox One
// model 1708 updated to the 2021+ firmware with Bluetooth LE). Older Xbox One
// firmware uses Classic Bluetooth only and will not show up — update it with
// the Xbox Accessories app first.
//
// PAIRING: hold the small pair button on top of the controller until the Xbox
// logo flashes fast. The robot accepts a NEW controller when:
//   - no controller has been paired yet, or
//   - during the first GAMEPAD_BOOT_PAIR_WINDOW_MS after power-on, or
//   - after you press "Pair controller" in the web UI (Settings → Bluetooth).
// Once paired, just press the Xbox button to reconnect — no pairing needed.
// Outside those windows the robot ignores other people's controllers, so
// several robots can share a room.
//
// BUTTON MAPPING:
//   Left stick           Drive (arcade: up = forward, sideways = turn)
//   Right stick X        Turn (only when GAMEPAD_SPLIT_STICKS is true)
//   D-pad                Slow drive at GAMEPAD_DPAD_SPEED
//   LB (hold)            Precision mode: drive scaled by GAMEPAD_PRECISION_SCALE
//   RT (right trigger)   Weapon throttle — spins only while held (dead-man)
//   B                    Weapon stop (also latches until RT is released)
//   A                    Little error sound ("nope" beep)
//
// Safety: the weapon ignores RT until it has been fully released once after
// connecting, and drive + weapon stop immediately if the controller disconnects.
//
#define GAMEPAD_NAME_FILTER          "Xbox"  // advertised name must contain this to pair
#define GAMEPAD_BOOT_PAIR_WINDOW_MS  60000   // accept new controllers for 60 s after boot
#define GAMEPAD_PAIR_WINDOW_MS       60000   // "Pair controller" button opens a 60 s window
#define GAMEPAD_SPLIT_STICKS         false   // true = left stick Y drives, right stick X turns
#define GAMEPAD_STICK_DEADZONE       0.12f   // ignore stick wobble near center (0–1)
#define GAMEPAD_TRIGGER_DEADZONE     0.05f   // ignore trigger values below this (0–1)
#define GAMEPAD_PRECISION_SCALE      0.4f    // drive multiplier while LB is held
#define GAMEPAD_DPAD_SPEED           0.4f    // drive speed for D-pad driving
#define GAMEPAD_SCAN_INTERVAL_MS     100     // BLE scan interval (ms)
#define GAMEPAD_SCAN_WINDOW_MS       40      // time listening per interval (ms) — keeps WiFi responsive
#define GAMEPAD_UI_INTERVAL_MS       30      // min time between live controller updates to the browser
                                             // (~33/s; only sent when the input changed). The browser
                                             // smooths between updates, so going lower gains little.
#define BT_NEARBY_SCAN_MS            15000   // "Nearby devices" scan length (BLE tab)
#define BT_NEARBY_UPDATE_MS          1000    // list refresh to the browser while scanning
#define BT_INFO_INTERVAL_MS          2000    // how often the Bluetooth tab's device details
                                             // (battery, signal, link…) are sent to the browser
