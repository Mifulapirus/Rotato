#pragma once
// =============================================================================
// StatusLed.h — On-board blue LED status patterns (non-blocking)
// =============================================================================
// Copyright (c) 2026 Angel Hernandez (www.thehomelab.dev)
// Non-Commercial Source-Available License — see LICENSE for full terms.
// Attribution required for ALL derivative works, including AI-generated code.
//
// Drives the D1 Mini's blue LED (PIN_ONBOARD_LED) with simple blink patterns.
// main.cpp decides WHICH pattern to show (see the priority table in config.h);
// this class only plays it.
//
// HOW IT WORKS:
//   A pattern is a 40-bit mask. Each bit is a 50 ms time slot, so one full
//   pattern lasts 2 seconds and then repeats. Bit 0 is the first slot.
//   A 1 bit = LED on during that slot.
//
// USAGE:
//   statusLed.begin();
//   statusLed.setPattern(StatusLed::Pattern::HEARTBEAT);
//   // loop():
//   statusLed.update();
// =============================================================================

#include <Arduino.h>
#include "config.h"

class StatusLed {
public:
    enum class Pattern : uint8_t {
        OFF,
        SOLID,          // someone is connected
        HEARTBEAT,      // idle — firmware alive, nobody connected
        PAIRING,        // controller pairing window open
        ESC_ARMING,     // boot: ESC arming hold
        BATTERY_LOW,    // battery low / missing
        WEAPON,         // weapon spinning
    };

    void begin();
    void setPattern(Pattern p);
    Pattern getPattern() const { return _pattern; }

    // Call every loop() — advances the pattern using millis().
    void update();

private:
    Pattern  _pattern    = Pattern::OFF;
    uint32_t _start_ms   = 0;     // when the current pattern started
    int8_t   _lastOutput = -1;    // last value written (avoid redundant writes)

    void write(bool on);
};
