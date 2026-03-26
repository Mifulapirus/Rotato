// =============================================================================
// BuzzerController.cpp — Non-Blocking Buzzer Sound Sequences (Implementation)
// =============================================================================

#include "BuzzerController.h"

#ifdef HAS_BUZZER

// ── Predefined sound sequences ─────────────────────────────────────────────
// Each sequence is an array of {frequency_hz, duration_ms} steps.
// Frequency 0 = silence (pause between beeps).

static const BeepStep SEQ_STARTUP[] = {
    {880, 100}, {0, 50}, {1174, 100}, {0, 50}, {1568, 200}
};

static const BeepStep SEQ_ARMED[] = {
    {1568, 80}, {0, 40}, {1568, 80}
};

static const BeepStep SEQ_WEAPON_ON[] = {
    {523, 60}, {659, 60}, {784, 120}
};

static const BeepStep SEQ_WEAPON_OFF[] = {
    {784, 60}, {523, 120}
};

static const BeepStep SEQ_ERROR[] = {
    {220, 200}, {0, 100}, {220, 200}, {0, 100}, {220, 400}
};

// ── Public methods ────────────────────────────────────────────────────────────

void BuzzerController::begin() {
    ledcSetup(LEDC_CH_BUZZER, 1000, 8);  // 8-bit resolution for tone (0-255)
    ledcAttachPin(PIN_GPIO7_SHARED, LEDC_CH_BUZZER);
    toneOff();
    Serial.println("[Buzzer] Initialized.");
}

void BuzzerController::beepStartup() {
    playSequence(SEQ_STARTUP, sizeof(SEQ_STARTUP) / sizeof(BeepStep));
}

void BuzzerController::beepArmed() {
    playSequence(SEQ_ARMED, sizeof(SEQ_ARMED) / sizeof(BeepStep));
}

void BuzzerController::beepWeaponOn() {
    playSequence(SEQ_WEAPON_ON, sizeof(SEQ_WEAPON_ON) / sizeof(BeepStep));
}

void BuzzerController::beepWeaponOff() {
    playSequence(SEQ_WEAPON_OFF, sizeof(SEQ_WEAPON_OFF) / sizeof(BeepStep));
}

void BuzzerController::beepError() {
    playSequence(SEQ_ERROR, sizeof(SEQ_ERROR) / sizeof(BeepStep));
}

void BuzzerController::update() {
    // Nothing to do if no sequence is playing
    if (_stepIndex >= _sequenceLength) return;

    uint32_t now = millis();

    // Check if the current step has finished
    if (now - _stepStart_ms >= _sequence[_stepIndex].duration_ms) {
        _stepIndex++;
        _stepStart_ms = now;

        if (_stepIndex < _sequenceLength) {
            // Advance to next step
            const BeepStep& step = _sequence[_stepIndex];
            if (step.freq_hz > 0) {
                toneOn(step.freq_hz);
            } else {
                toneOff();
            }
        } else {
            // Sequence complete — silence the buzzer
            toneOff();
        }
    }
}

// ── Private methods ────────────────────────────────────────────────────────────

void BuzzerController::playSequence(const BeepStep* steps, uint8_t length) {
    _sequence       = steps;
    _sequenceLength = length;
    _stepIndex      = 0;
    _stepStart_ms   = millis();

    // Start the first step immediately
    if (steps[0].freq_hz > 0) {
        toneOn(steps[0].freq_hz);
    } else {
        toneOff();
    }
}

void BuzzerController::toneOn(uint16_t freq_hz) {
    // Change LEDC frequency and set 50% duty to produce a tone
    ledcWriteTone(LEDC_CH_BUZZER, freq_hz);
}

void BuzzerController::toneOff() {
    ledcWriteTone(LEDC_CH_BUZZER, 0);
}

#endif // HAS_BUZZER
