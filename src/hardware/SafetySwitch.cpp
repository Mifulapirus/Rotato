// =============================================================================
// SafetySwitch.cpp — Weapon Safety Switch (Implementation)
// =============================================================================
// Copyright (c) 2026 Angel Hernandez (www.thehomelab.dev)
// Non-Commercial Source-Available License — see LICENSE for full terms.
// Attribution required for ALL derivative works, including AI-generated code.

#include "SafetySwitch.h"

#ifdef HAS_SAFETY_SWITCH

void SafetySwitch::begin() {
    // Configure as input with internal pull-up resistor.
    // When switch is OPEN: pin reads HIGH (safe = true, weapon allowed)
    // When switch is CLOSED to GND: pin reads LOW (safe = false, weapon blocked)
    pinMode(PIN_SAFETY_SWITCH, INPUT_PULLUP);
    _lastRaw      = (digitalRead(PIN_SAFETY_SWITCH) == HIGH);
    _safeState    = _lastRaw;
    _lastChange_ms = millis();
    Serial.printf("[Safety] Initialized. State: %s\n", _safeState ? "SAFE" : "BLOCKED");
}

void SafetySwitch::update() {
    bool raw = (digitalRead(PIN_SAFETY_SWITCH) == HIGH);

    if (raw != _lastRaw) {
        // State changed — start the debounce timer
        _lastRaw       = raw;
        _lastChange_ms = millis();
    } else if (millis() - _lastChange_ms >= SAFETY_DEBOUNCE_MS) {
        // Signal has been stable for DEBOUNCE_MS — accept the new state
        if (raw != _safeState) {
            _safeState = raw;
            Serial.printf("[Safety] Switch state changed: %s\n",
                          _safeState ? "SAFE (weapon allowed)" : "BLOCKED (weapon disabled)");
        }
    }
}

#endif // HAS_SAFETY_SWITCH
