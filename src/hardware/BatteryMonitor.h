#pragma once
// =============================================================================
// BatteryMonitor.h — LiPo Battery Voltage Monitor
// =============================================================================
//
// Reads battery voltage from the analog voltage divider connected to GPIO0.
//
// CIRCUIT:
//   Battery (+) ── R1 (20KΩ) ── ADC PIN ── R2 (10KΩ) ── GND
//
//   The divider reduces the 7.4V battery voltage to a safe range for the ADC:
//     V_adc = V_batt × R2 / (R1 + R2) = V_batt × 10K / 30K = V_batt / 3
//     V_batt = V_adc × 3
//
// ROLLING AVERAGE:
//   ADC readings can be noisy. We collect BATTERY_NUM_SAMPLES readings and
//   return the average. This makes the voltage display smooth and stable.
//
// VOLTAGE → PERCENTAGE:
//   2S LiPo: 8.4V = 100% charge, 6.0V = 0% (safe minimum)
//   Anything outside [6.0, 8.4] is clamped to [0, 100].
// =============================================================================

#include <Arduino.h>
#include <Preferences.h>
#include "config.h"

class BatteryMonitor {
public:
    // Configure the ADC pin and load saved calibration ratio from NVS.
    void begin();

    // Read ADC, update rolling average.
    // Call this periodically (e.g. every 500ms) from the main loop.
    void update();

    // Returns the averaged battery voltage in volts (e.g. 7.8)
    float getVoltage() const { return _voltageV; }

    // Returns the battery level as a percentage (0–100)
    uint8_t getPercent() const;

    // Returns true if battery is below safe minimum (BATTERY_VOLTAGE_EMPTY)
    bool isLow() const { return _voltageV < BATTERY_VOLTAGE_EMPTY; }

    // Returns the current voltage-divider ratio used for conversion.
    // Default is BATTERY_R_RATIO from config.h; can be overridden at runtime.
    float getRatio() const { return _rRatio; }

    // Update the voltage-divider ratio and persist it to NVS so it survives
    // reboots.  Immediately recomputes the last voltage reading.
    // ratio must be > 0 and <= 10; values outside that range are ignored.
    void setRatio(float ratio);

private:
    float       _samples[BATTERY_NUM_SAMPLES] = {};  // Circular buffer of readings
    uint8_t     _sampleIndex = 0;                    // Next write position
    float       _voltageV    = 7.4f;                 // Current averaged voltage
    bool        _filled      = false;                // Has the buffer filled once?
    float       _rRatio      = BATTERY_R_RATIO;      // Runtime-adjustable divider ratio
    Preferences _prefs;                              // NVS storage

    // Convert raw 12-bit ADC value to battery voltage using _rRatio
    float rawToVoltage(int raw) const;
};
