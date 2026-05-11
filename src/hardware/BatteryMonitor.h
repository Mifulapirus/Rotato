#pragma once
// =============================================================================
// BatteryMonitor.h — LiPo Battery Voltage Monitor
// =============================================================================
//
// Reads battery voltage from the analog voltage divider connected to GPIO0.
//
// CIRCUIT:
//   Battery (+) ── R1 (39KΩ) ── ADC PIN ── R2 (10KΩ) ── GND
//
//   V_adc = V_batt × 10K / 49K   →   V_batt = V_adc × 4.9
//   Works for both 2S (max 8.4V → 1.71V ADC) and 3S (max 12.6V → 2.57V ADC)
//
// ROLLING AVERAGE:
//   ADC readings can be noisy. We collect BATTERY_NUM_SAMPLES readings and
//   return the average. This makes the voltage display smooth and stable.
//
// VOLTAGE → PERCENTAGE:
//   2S LiPo: 8.4V = 100% charge, 6.0V = 0% (safe minimum)
//   3S LiPo: 12.6V = 100% charge, 9.0V = 0% (safe minimum)
//   Battery type is selectable at runtime (saved to NVS). Default: BATTERY_DEFAULT_CELLS.
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

    // Returns true if battery is below safe minimum for the selected battery type.
    bool isLow() const { return _voltageV < _voltEmpty; }

    // Returns the current voltage-divider ratio used for conversion.
    // Default is BATTERY_R_RATIO from config.h; can be overridden at runtime.
    float getRatio() const { return _rRatio; }

    // Update the voltage-divider ratio and persist it to NVS so it survives
    // reboots.  Immediately recomputes the last voltage reading.
    // ratio must be > 0 and <= 10; values outside that range are ignored.
    void setRatio(float ratio);

    // Returns the selected battery cell count (2 or 3).
    uint8_t getBatteryCells() const { return _cells; }

    // Set battery type (2 = 2S, 3 = 3S) and persist to NVS.
    // Updates the voltage full/empty thresholds used for percent calculation.
    void setBatteryCells(uint8_t cells);

private:
    float       _samples[BATTERY_NUM_SAMPLES] = {};  // Circular buffer of readings
    uint8_t     _sampleIndex = 0;                    // Next write position
    float       _voltageV    = 0.0f;                 // Current averaged voltage
    bool        _filled      = false;                // Has the buffer filled once?
    float       _rRatio      = BATTERY_R_RATIO;      // Runtime-adjustable divider ratio
    uint8_t     _cells       = BATTERY_DEFAULT_CELLS; // 2 = 2S, 3 = 3S
    float       _voltFull    = BATTERY_VOLTAGE_FULL_3S;  // Full-charge threshold (V)
    float       _voltEmpty   = BATTERY_VOLTAGE_EMPTY_3S; // Safe-minimum threshold (V)
    Preferences _prefs;                              // NVS storage

    // Convert raw 12-bit ADC value to battery voltage using _rRatio
    float rawToVoltage(int raw) const;
};
