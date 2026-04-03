// =============================================================================
// BatteryMonitor.cpp — LiPo Battery Voltage Monitor (Implementation)
// =============================================================================

#include "BatteryMonitor.h"

static const char* BATT_NVS_NS    = "robot_batt";
static const char* BATT_NVS_RATIO = "r_ratio";

void BatteryMonitor::begin() {
    // Load saved calibration ratio (or fall back to compiled-in default)
    _prefs.begin(BATT_NVS_NS, true);
    _rRatio = _prefs.getFloat(BATT_NVS_RATIO, BATTERY_R_RATIO);
    _prefs.end();
    Serial.printf("[Battery] Using ratio: %.4f\n", _rRatio);

    // Set GPIO0 as analog input.
    // Use 11dB attenuation to extend the measurable range to ~3.1V on the ADC pin.
    // (Battery max = 8.4V → V_adc_max = 8.4 / 3 = 2.8V, within 3.1V range)
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
    // Map voltage from [EMPTY → FULL] to [0 → 100]
    float range = BATTERY_VOLTAGE_FULL - BATTERY_VOLTAGE_EMPTY;
    float percent = (_voltageV - BATTERY_VOLTAGE_EMPTY) / range * 100.0f;

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

float BatteryMonitor::rawToVoltage(int raw) const {
    // Convert 12-bit ADC reading to actual battery voltage.
    // V_adc  = (raw / 4095) × VREF
    // V_batt = V_adc × _rRatio   (undo the voltage divider)
    float v_adc = ((float)raw / (float)BATTERY_ADC_BITS) * BATTERY_ADC_VREF;
    return v_adc * _rRatio;
}
