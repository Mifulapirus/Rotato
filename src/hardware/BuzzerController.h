#pragma once
// =============================================================================
// BuzzerController.h — Non-Blocking Buzzer Sound Sequences
// =============================================================================
//
// Controls the passive buzzer connected to GPIO7 (via JP1 solder jumper).
// Only compiled/used when HAS_BUZZER is defined in config.h.
//
// IMPORTANT: This class is NON-BLOCKING. It does NOT use delay().
// Instead, it uses millis() to track timing, so the rest of the robot
// keeps running while a beep sequence plays.
//
// Call update() every loop iteration for this to work correctly.
//
// HOW IT WORKS:
//   1. You call beepStartup(), beepArmed(), etc.
//   2. update() checks millis() and steps through the sequence.
//   3. Each "step" turns the buzzer on or off for a set duration.
//
// BUZZER TYPE: Passive buzzer (requires a PWM signal to make sound).
//   The ESP32 LEDC peripheral generates a square wave at the given frequency.
//   Frequency determines the pitch; duty cycle can be 50% for loudest tone.
// =============================================================================

#include <Arduino.h>
#include "config.h"

#ifdef HAS_BUZZER

// A single step in a beep sequence: frequency in Hz (0 = silence), duration in ms
struct BeepStep {
    uint16_t freq_hz;   // Tone frequency. 0 = buzzer off.
    uint16_t duration_ms;
};

class BuzzerController {
public:
    // Initialize the LEDC channel for the buzzer pin
    void begin();

    // ── Predefined sequences ─────────────────────────────────────────────────
    void beepStartup();     // Robot powered on and ready
    void beepArmed();       // Robot ready to drive
    void beepWeaponOn();    // Weapon activated (warning tone)
    void beepWeaponOff();   // Weapon deactivated
    void beepError();       // Something went wrong

    // Must be called from loop() — advances the current beep sequence
    void update();

    // Returns true if a sequence is currently playing
    bool isBusy() const { return _stepIndex < _sequenceLength; }

private:
    // Start playing a custom sequence
    void playSequence(const BeepStep* steps, uint8_t length);

    // Turn buzzer tone on at given frequency
    void toneOn(uint16_t freq_hz);

    // Turn buzzer off (silence)
    void toneOff();

    const BeepStep* _sequence     = nullptr;
    uint8_t         _sequenceLength = 0;
    uint8_t         _stepIndex      = 0;
    uint32_t        _stepStart_ms   = 0;
};

#else // HAS_BUZZER not defined — provide empty stub so code compiles either way

class BuzzerController {
public:
    void begin()         {}
    void beepStartup()   {}
    void beepArmed()     {}
    void beepWeaponOn()  {}
    void beepWeaponOff() {}
    void beepError()     {}
    void update()        {}
    bool isBusy() const  { return false; }
};

#endif // HAS_BUZZER
