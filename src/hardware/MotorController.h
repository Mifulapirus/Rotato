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
#include <Preferences.h>
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

    // Activate or deactivate the weapon motor at full speed (ON/OFF button mode).
    //   active = true  → ramp to full speed
    //   active = false → ramp to stop
    void setWeapon(bool active);

    // Set weapon target speed directly (0.0 = off, 1.0 = full). Used by slider mode.
    // The firmware ramps the actual PWM output toward this target at the configured rate.
    void setWeaponSpeed(float speed);

    // Emergency stop: set all three motors to neutral/idle immediately.
    // Called by safety switch or on disconnect. Also aborts any ESC calibration.
    void stopAll();

    // Returns true if weapon motor target speed > 0
    bool isWeaponActive() const { return _weaponActive; }

    // Current ramped output speed (0.0–1.0). Lags behind target during a ramp.
    float getWeaponCurrentSpeed() const { return _weaponCurrentSpeed; }

    // ── Weapon acceleration ───────────────────────────────────────────────────
    // rampMs: time (ms) for a full 0→100% sweep. 0 = instant.
    // curve : 0=linear, 1=quadratic (x²), 2=s-curve (smoothstep)
    // Both are persisted to NVS and applied immediately.
    void     setWeaponRamp(uint16_t rampMs);
    void     setWeaponCurve(uint8_t curve);
    uint16_t getWeaponRampMs() const { return _weaponRampMs; }
    uint8_t  getWeaponCurve()  const { return _weaponCurve; }

    // ── ESC initialization ────────────────────────────────────────────────────
    // On power-on, the firmware holds PWM_WEAPON_IDLE_US for ESC_INIT_DELAY_MS
    // before allowing any weapon commands. Call updateEscState() every loop().

    // Advance the ESC init timer and calibration state machine. Call every loop().
    void updateEscState();

    // Returns true once the boot hold time has elapsed and the ESC is armed.
    bool isEscReady() const { return _escReady; }

    // ── ESC calibration ───────────────────────────────────────────────────────
    // Full-range calibration: output max throttle for ESC_CALIB_HIGH_MS, then
    // min throttle for ESC_CALIB_LOW_MS. The ESC learns the signal endpoints.
    // Disconnect the weapon motor before calling this.
    // Returns false if not ready or a calibration is already running.
    bool startEscFullCalib();

    // True while a calibration sequence is in progress.
    bool isEscCalibrating() const { return _escCalib != EscCalib::IDLE && _escCalib != EscCalib::DONE; }

    // String label for the current calibration step: "idle", "high", "low", "done".
    const char* escCalibStr() const;

    // Set the drive pulse-width half-range (µs from neutral 1500).
    // Default = PWM_DRIVE_HALF_RANGE_US (500 = standard 1000–2000 µs window).
    // Range is clamped to [200, 950] to stay safely inside most ESC windows.
    // Call this from the settings panel "Servo Range" control.
    void setDriveRange(uint16_t halfRangeUs);

    // Return current half-range setting
    uint16_t getDriveRange() const { return _halfRangeUs; }

private:
    bool     _weaponActive = false;
    uint16_t _halfRangeUs  = PWM_DRIVE_HALF_RANGE_US;  // runtime-adjustable drive range

    // Weapon speed ramp
    float    _weaponTargetSpeed  = 0.0f;               // 0–1 requested by user
    float    _weaponCurrentSpeed = 0.0f;               // 0–1 currently output to PWM
    uint16_t _weaponRampMs       = WEAPON_RAMP_MS_DEFAULT;
    uint8_t  _weaponCurve        = WEAPON_CURVE_DEFAULT;
    uint32_t _lastRampTick_ms    = 0;
    Preferences _weaponPrefs;

    // ESC arming state
    bool     _escReady     = false;
    uint32_t _escInitStart = 0;      // millis() captured in begin()

    // ESC calibration state machine (CAL_HIGH/CAL_LOW avoid clashing with Arduino HIGH/LOW macros)
    enum class EscCalib : uint8_t { IDLE, CAL_HIGH, CAL_LOW, DONE };
    EscCalib _escCalib      = EscCalib::IDLE;
    uint32_t _escCalibStart = 0;

    // Apply _weaponCurve to a 0–1 speed value, then write the PWM
    void applyWeaponPwm();
    // Step _weaponCurrentSpeed one tick toward _weaponTargetSpeed
    void stepWeaponRamp();

    // Convert a speed value (-1.0 to +1.0) to a PWM duty cycle (0–65535).
    // Optionally reverses direction if the motor is mounted mirrored.
    uint32_t speedToDuty(float speed, bool reversed);

    // Convert a raw pulse width in microseconds to a 16-bit LEDC duty value.
    //   duty = (pulse_us / PWM_PERIOD_US) × 65535
    uint32_t usToDuty(uint32_t pulse_us);

    // Write a duty value to an LEDC channel
    void writeDuty(uint8_t channel, uint32_t duty);
};
