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

    // Set GPIO0 as analog input.
    // Use 11dB attenuation to extend the measurable range to ~3.1V on the ADC pin.
    // (Battery max = 12.6V → V_adc_max = 12.6 / 4.9 = 2.57V, within 3.1V range)
    // Note: analogSetPinAttenuation is pin-specific (arduino-esp32 3.x preferred API).
    analogSetPinAttenuation(PIN_BATTERY_ADC, ADC_11db);
    pinMode(PIN_BATTERY_ADC, INPUT);

    // Warm up the buffer with initial readings
    for (uint8_t i = 0; i < BATTERY_NUM_SAMPLES; i++) {
        int raw = analogRead(PIN_BATTERY_ADC);
        _samples[i] = rawToVoltage(raw);
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
    int raw = analogRead(PIN_BATTERY_ADC);
    _samples[_sampleIndex] = rawToVoltage(raw);
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
        _samples[i] = rawToVoltage(analogRead(PIN_BATTERY_ADC));
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

float BatteryMonitor::rawToVoltage(int raw) const {
    // Convert 12-bit ADC reading to actual battery voltage.
    // V_adc  = (raw / 4095) × VREF
    // V_batt = V_adc × _rRatio   (undo the voltage divider)
    float v_adc = ((float)raw / (float)BATTERY_ADC_BITS) * BATTERY_ADC_VREF;
    return v_adc * _rRatio;
}
