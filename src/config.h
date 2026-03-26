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
//   - Uncomment HAS_BUZZER or HAS_SAFETY_SWITCH depending on JP1 solder jumper
//   - Change AP_SSID / AP_PASSWORD for your robot's WiFi name
// =============================================================================

// ── Feature Flags ─────────────────────────────────────────────────────────────
//
// GPIO7 is SHARED between the Buzzer (via solder jumper JP1) and the J10
// safety switch. You can only enable ONE at a time based on your hardware.
//
//   JP1 soldered   → #define HAS_BUZZER       (buzzer works, J10 ignored)
//   JP1 unsoldered → #define HAS_SAFETY_SWITCH (J10 works, no buzzer sound)
//
#define HAS_BUZZER
// #define HAS_SAFETY_SWITCH

// Enable/disable BLE (stub only — for future Android app development)
// #define BLE_ENABLED

// ── Pin Definitions ──────────────────────────────────────────────────────────
//
// ESP32-C3 Super Mini PCB pinout for this robot controller board.
//
#define PIN_BATTERY_ADC   0   // GPIO0  — Battery voltage divider (R1=20K, R2=10K)
#define PIN_MOTOR_LEFT    1   // GPIO1  — J5: Left wheel ESC signal
#define PIN_MOTOR_RIGHT   3   // GPIO3  — J6: Right wheel ESC signal
#define PIN_MOTOR_WEAPON  4   // GPIO4  — J7: Weapon motor ESC signal
#define PIN_EXPANSION_1   5   // GPIO5  — J8: Open/expansion (unused)
#define PIN_EXPANSION_2   6   // GPIO6  — J9: Open/expansion (unused)
#define PIN_GPIO7_SHARED  7   // GPIO7  — Buzzer (JP1) OR J10 safety switch

// ── PWM / RC Servo Timing ─────────────────────────────────────────────────────
//
// Standard RC/ESC protocol: 50Hz signal, pulse width 1000–2000 microseconds.
//   1000µs = full reverse  |  1500µs = stop/neutral  |  2000µs = full forward
//
// The ESP32 LEDC peripheral generates these pulses.
// Timer resolution is 16-bit: values 0–65535 represent 0–20ms (one full period).
//   Duty formula: duty = (pulse_us / 20000.0) * 65535
//
#define PWM_FREQUENCY_HZ    50      // 50 Hz = 20ms period (standard RC protocol)
#define PWM_RESOLUTION_BITS 16      // 16-bit resolution for smooth control
#define PWM_PERIOD_US       20000   // 20,000 µs = one full period
#define PWM_NEUTRAL_US      1500    // Stop / neutral position
#define PWM_MIN_US          1000    // Full reverse
#define PWM_MAX_US          2000    // Full forward
#define PWM_WEAPON_IDLE_US  1000    // Weapon off (minimum pulse = ESC disarmed)
#define PWM_WEAPON_FULL_US  2000    // Weapon full speed

// LEDC channels assigned to each motor (ESP32 has up to 8 channels)
#define LEDC_CH_LEFT_MOTOR    0
#define LEDC_CH_RIGHT_MOTOR   1
#define LEDC_CH_WEAPON_MOTOR  2
#define LEDC_CH_BUZZER        3     // Only used if HAS_BUZZER is defined

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
#define BATTERY_R_RATIO       3.0f  // Voltage divider multiplier (see above)
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
#define AP_PASSWORD       "roboto2026"  // Minimum 8 characters for WPA2
#define AP_IP_ADDRESS     "192.168.4.1" // Default ESP32 AP gateway address
#define STA_CONNECT_TIMEOUT_MS 10000   // 10 seconds to try connecting to home WiFi

// ── Web Server ────────────────────────────────────────────────────────────────
#define WEB_SERVER_PORT       80    // Standard HTTP port
#define WS_STATUS_INTERVAL_MS 500  // How often to send status to browser (ms)
