#pragma once
// =============================================================================
// config.h — Robot Controller Configuration
// =============================================================================
//
// This is the ONE FILE you need to change when adjusting hardware settings.
// All pin numbers, PWM timing values, WiFi credentials, and feature switches
// live here so students only have to look in one place.
//
// HOW TO USE:
//   - Change GPIO numbers if you wire things differently
//   - Buzzer is always on GPIO7 via JP1. If JP1 is unsoldered, GPIO7 is free expansion.
//   - Safety switch is on GPIO5 (J8) — independent of the buzzer jumper.
//   - Change AP_SSID / AP_PASSWORD for your robot's WiFi name
// =============================================================================

// ── Feature Flags ─────────────────────────────────────────────────────────────
//
// HAS_BUZZER        — enable if JP1 solder jumper is closed (GPIO7 → buzzer).
//                     If JP1 is open, GPIO7 is unused (expansion J9 area).
// HAS_SAFETY_SWITCH — enable if a kill-switch is wired to J8 (GPIO5).
//                     Both flags are independent and can be set at the same time.
//
#define HAS_BUZZER
#define HAS_SAFETY_SWITCH

// Enable/disable BLE (NimBLE-Arduino — connects the Android app to the robot)
#define BLE_ENABLED

// ── Pin Definitions ──────────────────────────────────────────────────────────
//
// ESP32-C3 Super Mini PCB pinout for this robot controller board.
//
#define PIN_BATTERY_ADC   0   // GPIO0  — Battery voltage divider (R1=20K, R2=10K)
#define PIN_MOTOR_LEFT    1   // GPIO1  — J5: Left wheel ESC signal
#define PIN_MOTOR_RIGHT   3   // GPIO3  — J6: Right wheel ESC signal
#define PIN_MOTOR_WEAPON  4   // GPIO4  — J7: Weapon motor ESC signal
#define PIN_SAFETY_SWITCH 5   // GPIO5  — J8: Weapon kill-switch (active LOW, INPUT_PULLUP)
#define PIN_EXPANSION_1   6   // GPIO6  — J9: Open/expansion (unused)
#define PIN_BUZZER        7   // GPIO7  — Buzzer via JP1. If JP1 open: unused expansion.
#define PIN_BOOT_BUTTON   9   // GPIO9  — Built-in BOOT button (active LOW, hold 3s to clear WiFi)

// ── PWM / RC Servo Timing ─────────────────────────────────────────────────────
//
// Standard RC/ESC protocol: 50Hz signal, pulse width 1000–2000 microseconds.
//   1000µs = full reverse  |  1500µs = stop/neutral  |  2000µs = full forward
//
// The ESP32 LEDC peripheral generates these pulses.
// Timer resolution is 14-bit: values 0–16383 represent 0–20ms (one full period).
// NOTE: ESP-IDF 4.4.x on ESP32-C3 caps LEDC resolution at 14 bits (max 16383).
//   Duty formula: duty = (pulse_us / 20000.0) * ((1 << PWM_RESOLUTION_BITS) - 1)
//
#define PWM_FREQUENCY_HZ    50      // 50 Hz = 20ms period (standard RC protocol)
#define PWM_RESOLUTION_BITS 14      // 14-bit resolution (ESP-IDF 4.4.x max for ESP32-C3)
#define PWM_PERIOD_US       20000   // 20,000 µs = one full period
#define PWM_NEUTRAL_US      1500    // Stop / neutral position
#define PWM_MIN_US          1000    // Full reverse
#define PWM_MAX_US          2000    // Full forward
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

// ── Drive pulse-width range (servo/ESC travel) ────────────────────────────────
//
// Standard ESCs respond to 1000–2000 µs (±500 µs from neutral 1500 µs).
// Some ESCs and servos accept a wider window (e.g. 800–2200 µs = ±700 µs)
// which gives more resolution and fuller travel.
//
// PWM_DRIVE_HALF_RANGE_US sets how far from neutral the drive signal swings:
//   pulse = NEUTRAL ± HALF_RANGE   (clamped to PWM_MIN_US / PWM_MAX_US)
//
// This value can also be changed at runtime via the web settings panel
// ("Servo Range" slider).  Increase if motors only move a small fraction of
// their travel at full stick deflection.
//
// Safe starting range: 500 (standard).  Typical max: 700.  Hard limit: 950
// (keeps pulse inside 550–2450 µs, well clear of most ESC arming windows).
//
#define PWM_DRIVE_HALF_RANGE_US  500   // ±µs from neutral (default = standard 1000–2000)

// LEDC channels assigned to each motor (ESP32 has up to 8 channels)
// IMPORTANT — channel-to-timer mapping on ESP32 (arduino-esp32 v2.x):
//   CH 0, 1 → Timer 0   CH 2, 3 → Timer 1   CH 4, 5 → Timer 2   CH 6, 7 → Timer 3
// ledcWriteTone() reconfigures the ENTIRE timer, so motors and buzzer must
// never share the same timer pair.
//   Left  motor : CH 0 → Timer 0  ┐ drive pair — same timer is fine
//   Right motor : CH 1 → Timer 0  ┘
//   Weapon motor: CH 2 → Timer 1  — isolated, no other channel on Timer 1
//   Buzzer      : CH 4 → Timer 2  — isolated, avoids corrupting weapon PWM
#define LEDC_CH_LEFT_MOTOR    0
#define LEDC_CH_RIGHT_MOTOR   1
#define LEDC_CH_WEAPON_MOTOR  2
#define LEDC_CH_BUZZER        4     // Must NOT share Timer 1 with weapon (CH 2–3)

// Motor direction flags — set to true if a motor spins the wrong way
// (Common in differential drive: one motor is mounted mirrored)
#define MOTOR_LEFT_REVERSED   false
#define MOTOR_RIGHT_REVERSED  false

// ── Battery Monitor ───────────────────────────────────────────────────────────
//
// 7.4V (2S LiPo) voltage divider: R1=20KΩ (top), R2=10KΩ (bottom)
//   V_adc = V_batt × R2 / (R1 + R2) = V_batt × 10K/30K = V_batt / 3
//   V_batt = V_adc × 3
//
// ESP32-C3 ADC reference = ~2.45V (with internal attenuation calibration).
// ADC is 12-bit: raw values 0–4095.
//
#define BATTERY_R_RATIO       3.64f  // Voltage divider multiplier (see above)
#define BATTERY_ADC_VREF      2.45f // ESP32-C3 ADC reference voltage (volts)
#define BATTERY_ADC_BITS      4095  // 12-bit ADC max value
#define BATTERY_VOLTAGE_FULL  8.4f  // 100% — 2S LiPo fully charged
#define BATTERY_VOLTAGE_EMPTY 6.0f  // 0%  — 2S LiPo minimum safe voltage
#define BATTERY_NUM_SAMPLES   8     // Number of samples for rolling average

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
