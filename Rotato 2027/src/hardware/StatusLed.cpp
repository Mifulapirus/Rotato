// =============================================================================
// StatusLed.cpp — On-board blue LED status patterns (Implementation)
// =============================================================================
// Copyright (c) 2026 Angel Hernandez (www.thehomelab.dev)
// Non-Commercial Source-Available License — see LICENSE for full terms.
// Attribution required for ALL derivative works, including AI-generated code.

#include "StatusLed.h"

static constexpr uint32_t SLOT_MS   = 50;   // one bit = 50 ms
static constexpr uint32_t NUM_SLOTS = 40;   // 40 × 50 ms = 2 s cycle

// Pattern bitmasks — bit N lights the LED during slot N (N × 50 ms … +50 ms).
static uint64_t patternMask(StatusLed::Pattern p) {
    switch (p) {
        case StatusLed::Pattern::SOLID:       return 0xFFFFFFFFFFULL;           // always on
        case StatusLed::Pattern::HEARTBEAT:   return 0x3ULL;                    // 100 ms blip every 2 s
        case StatusLed::Pattern::PAIRING:     return 0x003FF003FFULL;           // 500 ms on / 500 ms off
        case StatusLed::Pattern::ESC_ARMING:  return 0x3333333333ULL;           // 100 on / 100 off
        case StatusLed::Pattern::BATTERY_LOW: return (0x33ULL) | (0x33ULL << 20); // double blink each second
        case StatusLed::Pattern::WEAPON:      return 0x5555555555ULL;           // 50 on / 50 off strobe
        case StatusLed::Pattern::OFF:
        default:                              return 0;
    }
}

void StatusLed::begin() {
#ifdef HAS_ONBOARD_LED
    pinMode(PIN_ONBOARD_LED, OUTPUT);
    write(false);
    Serial.printf("[LED] Status LED on GPIO%d\n", PIN_ONBOARD_LED);
#endif
}

void StatusLed::setPattern(Pattern p) {
    if (p == _pattern) return;
    _pattern  = p;
    _start_ms = millis();   // restart so the new pattern begins at slot 0
}

void StatusLed::update() {
#ifdef HAS_ONBOARD_LED
    uint32_t slot = ((millis() - _start_ms) / SLOT_MS) % NUM_SLOTS;
    write((patternMask(_pattern) >> slot) & 1ULL);
#endif
}

void StatusLed::write(bool on) {
#ifdef HAS_ONBOARD_LED
    if ((int8_t)on == _lastOutput) return;
    _lastOutput = on;
    digitalWrite(PIN_ONBOARD_LED, (on == ONBOARD_LED_ACTIVE_HIGH) ? HIGH : LOW);
#endif
}
