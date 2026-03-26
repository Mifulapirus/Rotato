#pragma once
// =============================================================================
// SafetySwitch.h — Weapon Safety Switch (J10)
// =============================================================================
//
// Reads the hardware safety switch connected to GPIO7 via connector J10.
// Only compiled/used when HAS_SAFETY_SWITCH is defined in config.h.
//
// CIRCUIT:
//   GPIO7 ── J10 connector ── Switch ── GND
//   Internal pull-up resistor is enabled (INPUT_PULLUP).
//
// LOGIC:
//   Switch OPEN  (not pressed) → GPIO HIGH → weapon ALLOWED
//   Switch CLOSED (pressed)    → GPIO LOW  → weapon BLOCKED
//
// WHY A SAFETY SWITCH?
//   Even if the web UI says "activate weapon", if this switch is off the
//   weapon motor will NOT spin. It's a physical override — useful when
//   you're handling the robot and don't want it to accidentally injure you.
//
// DEBOUNCING:
//   Mechanical switches "bounce" — they rapidly switch on/off for ~10ms
//   when pressed. We wait for the signal to be stable for DEBOUNCE_MS before
//   accepting a new state.
// =============================================================================

#include <Arduino.h>
#include "config.h"

#ifdef HAS_SAFETY_SWITCH

#define SAFETY_DEBOUNCE_MS 20  // Ignore state changes shorter than this

class SafetySwitch {
public:
    // Configure GPIO7 as input with internal pull-up
    void begin();

    // Read and debounce the switch. Call from loop().
    void update();

    // Returns true if weapon is ALLOWED (switch open = HIGH)
    // Returns false if weapon is BLOCKED (switch closed = LOW)
    bool isSafe() const { return _safeState; }

private:
    bool     _safeState    = true;   // Current debounced state
    bool     _lastRaw      = true;   // Last raw GPIO reading
    uint32_t _lastChange_ms = 0;     // When the raw state last changed
};

#else // HAS_SAFETY_SWITCH not defined — weapon is always allowed

class SafetySwitch {
public:
    void begin()         {}
    void update()        {}
    bool isSafe() const  { return true; }  // Always safe (no switch connected)
};

#endif // HAS_SAFETY_SWITCH
