// =============================================================================
// BatteryMonitor.cpp — LiPo Battery Voltage Monitor (Implementation)
// =============================================================================
// Copyright (c) 2026 Angel Hernandez (www.thehomelab.dev)
// Non-Commercial Source-Available License — see LICENSE for full terms.
// Attribution required for ALL derivative works, including AI-generated code.

#include "BatteryMonitor.h"

static const char* BATT_NVS_NS    = "robot_batt";
static const char* BATT_NVS_RATIO = "r_ratio";
static const char* BATT_NVS_CELLS = "b_cells";

void BatteryMonitor::begin() {
    // Load saved calibration ratio (or fall back to compiled-in default)
    // Open read-write so the namespace is created on the very first boot;
    // read-only (true) would fail with NOT_FOUND on a blank NVS partition.
    _prefs.begin(BATT_NVS_NS, false);
    _rRatio = _prefs.getFloat(BATT_NVS_RATIO, BATTERY_R_RATIO);
    uint8_t savedCells = _prefs.getUChar(BATT_NVS_CELLS, BATTERY_DEFAULT_CELLS);
    _prefs.end();

    _cells     = (savedCells == 2) ? 2 : 3;
    _voltFull  = (_cells == 2) ? BATTERY_VOLTAGE_FULL_2S  : BATTERY_VOLTAGE_FULL_3S;
    _voltEmpty = (_cells == 2) ? BATTERY_VOLTAGE_EMPTY_2S : BATTERY_VOLTAGE_EMPTY_3S;
    Serial.printf("[Battery] Using ratio: %.4f, battery: %uS (%.1fV–%.1fV)\n",
                  _rRatio, _cells, _voltEmpty, _voltFull);

    // Configure the battery-sense pin (GPIO36 / A0) as an analog input.
    // 11dB attenuation extends the measurable range to ~2.5V (linear region).
    // (Battery max = 12.6V → V_adc_max = 12.6 / 5.7 = 2.21V)
    analogSetPinAttenuation(PIN_BATTERY_ADC, ADC_11db);
    pinMode(PIN_BATTERY_ADC, INPUT);

    // Warm up the buffer with initial readings
    for (uint8_t i = 0; i < BATTERY_NUM_SAMPLES; i++) {
        _samples[i] = readVoltage();
    }
    _filled = true;

    // Compute first valid average
    float sum = 0;
    for (uint8_t i = 0; i < BATTERY_NUM_SAMPLES; i++) sum += _samples[i];
    _voltageV = sum / BATTERY_NUM_SAMPLES;

    Serial.printf("[Battery] Initialized. Voltage: %.2fV (%d%%)\n",
                  _voltageV, getPercent());
}

void BatteryMonitor::update() {
    // Take a new reading and store in the circular buffer
    _samples[_sampleIndex] = readVoltage();
    _sampleIndex = (_sampleIndex + 1) % BATTERY_NUM_SAMPLES;

    // Compute rolling average across all samples
    float sum = 0;
    for (uint8_t i = 0; i < BATTERY_NUM_SAMPLES; i++) sum += _samples[i];
    _voltageV = sum / BATTERY_NUM_SAMPLES;
}

uint8_t BatteryMonitor::getPercent() const {
    // Map voltage from [_voltEmpty → _voltFull] to [0 → 100]
    float range   = _voltFull - _voltEmpty;
    float percent = (_voltageV - _voltEmpty) / range * 100.0f;

    // Clamp to valid percentage range
    if (percent < 0.0f)   return 0;
    if (percent > 100.0f) return 100;
    return (uint8_t)percent;
}

void BatteryMonitor::setRatio(float ratio) {
    if (ratio <= 0.0f || ratio > 10.0f) {
        Serial.printf("[Battery] setRatio(%.4f) rejected — out of range\n", ratio);
        return;
    }
    _rRatio = ratio;
    _prefs.begin(BATT_NVS_NS, false);
    _prefs.putFloat(BATT_NVS_RATIO, _rRatio);
    _prefs.end();
    Serial.printf("[Battery] Ratio updated to %.4f and saved to NVS.\n", _rRatio);
    // Refill the entire sample buffer with fresh ADC readings using the new ratio
    // so getVoltage() returns an accurate value immediately.
    for (uint8_t i = 0; i < BATTERY_NUM_SAMPLES; i++) {
        _samples[i] = readVoltage();
    }
    float sum = 0;
    for (uint8_t i = 0; i < BATTERY_NUM_SAMPLES; i++) sum += _samples[i];
    _voltageV    = sum / BATTERY_NUM_SAMPLES;
    _sampleIndex = 0;
    _filled      = true;
    Serial.printf("[Battery] Voltage after recalibration: %.2fV\n", _voltageV);
}

void BatteryMonitor::setBatteryCells(uint8_t cells) {
    if (cells != 2 && cells != 3) {
        Serial.printf("[Battery] setBatteryCells(%u) rejected — must be 2 or 3\n", cells);
        return;
    }
    _cells     = cells;
    _voltFull  = (_cells == 2) ? BATTERY_VOLTAGE_FULL_2S  : BATTERY_VOLTAGE_FULL_3S;
    _voltEmpty = (_cells == 2) ? BATTERY_VOLTAGE_EMPTY_2S : BATTERY_VOLTAGE_EMPTY_3S;
    _prefs.begin(BATT_NVS_NS, false);
    _prefs.putUChar(BATT_NVS_CELLS, _cells);
    _prefs.end();
    Serial.printf("[Battery] Battery type set to %uS (full=%.1fV, empty=%.1fV)\n",
                  _cells, _voltFull, _voltEmpty);
}

float BatteryMonitor::readVoltage() const {
    // analogReadMilliVolts() returns the pin voltage using the ESP32's factory
    // ADC calibration (eFuse), so no reference-voltage constant is needed.
    // V_batt = V_adc × _rRatio   (undo the voltage divider)
    float v_adc = (float)analogReadMilliVolts(PIN_BATTERY_ADC) / 1000.0f;
    return v_adc * _rRatio;
}
