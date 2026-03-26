#pragma once
// =============================================================================
// MotorController.h — Drive and Weapon Motor Control
// =============================================================================
//
// This class controls three motors using RC/ESC PWM signals via the ESP32
// LEDC peripheral:
//   - Left wheel motor  (J5)
//   - Right wheel motor (J6)
//   - Weapon motor      (J7)
//
// ARCADE DRIVE MIXING:
//   The web joystick gives us X (turn) and Y (forward/back) axes (-1.0 to 1.0).
//   We mix them into left/right wheel speeds using:
//     leftSpeed  = Y + X
//     rightSpeed = Y - X
//   Both are clamped to [-1.0, 1.0] then converted to PWM pulse widths.
//
// PWM PULSE MATH:
//   Period = 20,000 µs (50 Hz)
//   Duty  = (pulse_us / 20000.0) × 65535   (16-bit LEDC)
//   speed  0.0 → 1500µs (stop)
//   speed  1.0 → 2000µs (full forward)
//   speed -1.0 → 1000µs (full reverse)
// =============================================================================

#include <Arduino.h>
#include "config.h"

class MotorController {
public:
    // Initialize LEDC channels and set all motors to neutral/stopped
    void begin();

    // Drive the robot using joystick axes.
    //   x: turn  axis, -1.0 (left) to +1.0 (right)
    //   y: drive axis, -1.0 (backward) to +1.0 (forward)
    // Internally applies arcade mixing to derive left and right wheel speeds.
    void setDrive(float x, float y);

    // Activate or deactivate the weapon motor.
    //   active = true  → weapon spins at full speed
    //   active = false → weapon stops (ESC idle pulse)
    void setWeapon(bool active);

    // Emergency stop: set all three motors to neutral/idle immediately.
    // Called by safety switch or on disconnect.
    void stopAll();

    // Returns true if weapon motor is currently active
    bool isWeaponActive() const { return _weaponActive; }

private:
    bool _weaponActive = false;

    // Convert a speed value (-1.0 to +1.0) to a PWM duty cycle (0–65535).
    // Optionally reverses direction if the motor is mounted mirrored.
    uint32_t speedToDuty(float speed, bool reversed);

    // Convert a raw pulse width in microseconds to a 16-bit LEDC duty value.
    //   duty = (pulse_us / PWM_PERIOD_US) × 65535
    uint32_t usToDuty(uint32_t pulse_us);

    // Write a duty value to an LEDC channel
    void writeDuty(uint8_t channel, uint32_t duty);
};
