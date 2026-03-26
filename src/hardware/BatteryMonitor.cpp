// =============================================================================
// BatteryMonitor.cpp — LiPo Battery Voltage Monitor (Implementation)
// =============================================================================

#include "BatteryMonitor.h"

void BatteryMonitor::begin() {
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

float BatteryMonitor::rawToVoltage(int raw) const {
    // Convert 12-bit ADC reading to actual battery voltage.
    // V_adc  = (raw / 4095) × VREF
    // V_batt = V_adc × BATTERY_R_RATIO   (undo the voltage divider)
    float v_adc = ((float)raw / (float)BATTERY_ADC_BITS) * BATTERY_ADC_VREF;
    return v_adc * BATTERY_R_RATIO;
}
