// =============================================================================
// MotorController.cpp — Drive and Weapon Motor Control (Implementation)
// =============================================================================

#include "MotorController.h"

// ── Private helpers ──────────────────────────────────────────────────────────

uint32_t MotorController::usToDuty(uint32_t pulse_us) {
    // Convert pulse width (µs) to 16-bit duty cycle for the LEDC peripheral.
    // Formula: duty = (pulse_us / period_us) × (2^resolution - 1)
    return (uint32_t)((float)pulse_us / (float)PWM_PERIOD_US * 65535.0f);
}

uint32_t MotorController::speedToDuty(float speed, bool reversed) {
    // Clamp speed to valid range
    if (speed >  1.0f) speed =  1.0f;
    if (speed < -1.0f) speed = -1.0f;

    // Optionally invert for mirrored motors (see MOTOR_LEFT/RIGHT_REVERSED in config.h)
    if (reversed) speed = -speed;

    // Map [-1.0, 1.0] → [PWM_MIN_US, PWM_MAX_US]
    //   speed  0.0 → 1500µs (neutral/stop)
    //   speed  1.0 → 2000µs (full forward)
    //   speed -1.0 → 1000µs (full reverse)
    uint32_t pulse_us = (uint32_t)(PWM_NEUTRAL_US + speed * 500.0f);
    return usToDuty(pulse_us);
}

void MotorController::writeDuty(uint8_t channel, uint32_t duty) {
    ledcWrite(channel, duty);
}

// ── Public methods ────────────────────────────────────────────────────────────

void MotorController::begin() {
    // Set up LEDC timer and attach channels to their GPIO pins.
    // The LEDC peripheral is ESP32's hardware PWM generator.

    ledcSetup(LEDC_CH_LEFT_MOTOR,   PWM_FREQUENCY_HZ, PWM_RESOLUTION_BITS);
    ledcSetup(LEDC_CH_RIGHT_MOTOR,  PWM_FREQUENCY_HZ, PWM_RESOLUTION_BITS);
    ledcSetup(LEDC_CH_WEAPON_MOTOR, PWM_FREQUENCY_HZ, PWM_RESOLUTION_BITS);

    ledcAttachPin(PIN_MOTOR_LEFT,   LEDC_CH_LEFT_MOTOR);
    ledcAttachPin(PIN_MOTOR_RIGHT,  LEDC_CH_RIGHT_MOTOR);
    ledcAttachPin(PIN_MOTOR_WEAPON, LEDC_CH_WEAPON_MOTOR);

    // Start with all motors stopped
    stopAll();

    Serial.println("[Motors] Initialized. All motors at neutral.");
}

void MotorController::setDrive(float x, float y) {
    // ── Arcade Drive Mixing ───────────────────────────────────────────────────
    // Arcade drive takes a single joystick (X=turn, Y=drive) and computes
    // independent left and right motor speeds.
    //
    //   Left  speed = Y + X   (forward + turn right = left wheel speeds up)
    //   Right speed = Y - X   (forward + turn right = right wheel slows down)
    //
    // Example: push joystick forward-right (Y=0.8, X=0.5)
    //   Left  = 0.8 + 0.5 = 1.3 → clamped to 1.0 (full forward)
    //   Right = 0.8 - 0.5 = 0.3 (partial forward)
    //   Result: robot turns right while going forward ✓

    float leftSpeed  = y + x;
    float rightSpeed = y - x;

    // Clamp to [-1.0, 1.0] — values over 1.0 can occur from the mixing
    if (leftSpeed  >  1.0f) leftSpeed  =  1.0f;
    if (leftSpeed  < -1.0f) leftSpeed  = -1.0f;
    if (rightSpeed >  1.0f) rightSpeed =  1.0f;
    if (rightSpeed < -1.0f) rightSpeed = -1.0f;

    writeDuty(LEDC_CH_LEFT_MOTOR,  speedToDuty(leftSpeed,  MOTOR_LEFT_REVERSED));
    writeDuty(LEDC_CH_RIGHT_MOTOR, speedToDuty(rightSpeed, MOTOR_RIGHT_REVERSED));

    Serial.printf("[Motors] Drive x=%.2f y=%.2f → L=%.2f R=%.2f\n",
                  x, y, leftSpeed, rightSpeed);
}

void MotorController::setWeapon(bool active) {
    _weaponActive = active;
    uint32_t pulse_us = active ? PWM_WEAPON_FULL_US : PWM_WEAPON_IDLE_US;
    writeDuty(LEDC_CH_WEAPON_MOTOR, usToDuty(pulse_us));
    Serial.printf("[Motors] Weapon %s\n", active ? "ON" : "OFF");
}

void MotorController::stopAll() {
    // Send neutral (1500µs) to drive motors and idle (1000µs) to weapon
    writeDuty(LEDC_CH_LEFT_MOTOR,  usToDuty(PWM_NEUTRAL_US));
    writeDuty(LEDC_CH_RIGHT_MOTOR, usToDuty(PWM_NEUTRAL_US));
    writeDuty(LEDC_CH_WEAPON_MOTOR, usToDuty(PWM_WEAPON_IDLE_US));
    _weaponActive = false;
    Serial.println("[Motors] All stopped.");
}
