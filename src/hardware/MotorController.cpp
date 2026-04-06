// =============================================================================
// MotorController.cpp — Drive and Weapon Motor Control (Implementation)
// =============================================================================

#include "MotorController.h"

// ── Private helpers ──────────────────────────────────────────────────────────

uint32_t MotorController::usToDuty(uint32_t pulse_us) {
    // Convert pulse width (µs) to duty cycle for the LEDC peripheral.
    // Formula: duty = (pulse_us / period_us) × (2^resolution - 1)
    return (uint32_t)((float)pulse_us / (float)PWM_PERIOD_US * (float)((1u << PWM_RESOLUTION_BITS) - 1));
}

uint32_t MotorController::speedToDuty(float speed, bool reversed) {
    // Clamp speed to valid range
    if (speed >  1.0f) speed =  1.0f;
    if (speed < -1.0f) speed = -1.0f;

    // Optionally invert for mirrored motors (see MOTOR_LEFT/RIGHT_REVERSED in config.h)
    if (reversed) speed = -speed;

    // Map [-1.0, 1.0] → [NEUTRAL - halfRange, NEUTRAL + halfRange]
    //   speed  0.0 → 1500µs (neutral/stop)
    //   speed  1.0 → 1500 + _halfRangeUs µs (full forward)
    //   speed -1.0 → 1500 - _halfRangeUs µs (full reverse)
    //
    // NOTE: Do NOT clamp to PWM_MIN_US/PWM_MAX_US here — that would silently
    // override the user-configured extended range.  setDriveRange() already
    // validates _halfRangeUs ≤ 950, so the computed pulse stays within
    // 550–2450 µs which is safe for all common ESCs and servos.
    // An absolute hardware-safety clamp (500–2500 µs) is intentionally wide.
    uint32_t pulse_us = (uint32_t)(PWM_NEUTRAL_US + speed * (float)_halfRangeUs);
    if (pulse_us < 500u)   pulse_us = 500u;    // absolute floor — never below 500 µs
    if (pulse_us > 2500u)  pulse_us = 2500u;   // absolute ceiling — never above 2500 µs
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

    // Drive motors start at neutral (1500 µs)
    writeDuty(LEDC_CH_LEFT_MOTOR,  usToDuty(PWM_NEUTRAL_US));
    writeDuty(LEDC_CH_RIGHT_MOTOR, usToDuty(PWM_NEUTRAL_US));

    // ── Weapon ESC arming ────────────────────────────────────────────────────
    // RC ESCs require the PWM signal to be held at minimum throttle (1000 µs)
    // at power-on before they will accept commands. We hold this for
    // ESC_INIT_DELAY_MS and block all weapon commands until ready.
    _escReady     = false;
    _escInitStart = millis();
    writeDuty(LEDC_CH_WEAPON_MOTOR, usToDuty(PWM_WEAPON_IDLE_US));

    Serial.printf("[Motors] Weapon ESC arming — holding %u µs for %u ms.\n",
                  PWM_WEAPON_IDLE_US, ESC_INIT_DELAY_MS);
    Serial.println("[Motors] Drive motors initialized at neutral.");
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
    setWeaponSpeed(active ? 1.0f : 0.0f);
}

void MotorController::setWeaponSpeed(float speed) {
    if (!_escReady) {
        Serial.println("[Motors] Weapon blocked — ESC still arming.");
        return;
    }
    if (isEscCalibrating()) {
        Serial.println("[Motors] Weapon blocked — ESC calibration in progress.");
        return;
    }
    if (speed < 0.0f) speed = 0.0f;
    if (speed > 1.0f) speed = 1.0f;
    _weaponTargetSpeed = speed;
    _weaponActive      = (speed > 0.0f);
    if (_weaponRampMs == 0) {
        // Instant: bypass ramp, write immediately
        _weaponCurrentSpeed = speed;
        applyWeaponPwm();
    }
    // else: ramp will advance in updateEscState() each loop tick
    Serial.printf("[Motors] Weapon target %.2f %s\n", speed, _weaponRampMs ? "(ramping)" : "(instant)");
}

void MotorController::stopAll() {
    // Send neutral (1500µs) to drive motors and idle (1000µs) to weapon.
    // Immediately kills ramp and calibration.
    writeDuty(LEDC_CH_LEFT_MOTOR,   usToDuty(PWM_NEUTRAL_US));
    writeDuty(LEDC_CH_RIGHT_MOTOR,  usToDuty(PWM_NEUTRAL_US));
    writeDuty(LEDC_CH_WEAPON_MOTOR, usToDuty(PWM_WEAPON_IDLE_US));
    _weaponActive       = false;
    _weaponTargetSpeed  = 0.0f;
    _weaponCurrentSpeed = 0.0f;
    if (_escCalib == EscCalib::CAL_HIGH || _escCalib == EscCalib::CAL_LOW) {
        _escCalib = EscCalib::IDLE;
        Serial.println("[Motors] ESC calibration aborted by stopAll.");
    }
    Serial.println("[Motors] All stopped.");
}

void MotorController::setDriveRange(uint16_t halfRangeUs) {
    if (halfRangeUs < 200)  halfRangeUs = 200;   // floor: avoids very twitchy near-zero range
    if (halfRangeUs > 950)  halfRangeUs = 950;   // ceiling: keeps pulse inside ~550–2450 µs
    _halfRangeUs = halfRangeUs;
    Serial.printf("[Motors] Drive range set to ±%u µs (pulse: %u–%u µs)\n",
                  _halfRangeUs, PWM_NEUTRAL_US - _halfRangeUs, PWM_NEUTRAL_US + _halfRangeUs);
}

// ── ESC initialization & calibration ─────────────────────────────────────────

void MotorController::updateEscState() {
    // Phase 1 — boot arming: wait for ESC_INIT_DELAY_MS to elapse
    if (!_escReady) {
        if (millis() - _escInitStart >= ESC_INIT_DELAY_MS) {
            _escReady = true;
            Serial.println("[Motors] Weapon ESC armed and ready.");
        }
        return;  // Don't process calibration until fully armed
    }

    // Phase 2 — calibration state machine (only active when triggered by user)
    uint32_t now = millis();
    switch (_escCalib) {
        case EscCalib::CAL_HIGH:
            if (now - _escCalibStart >= ESC_CALIB_HIGH_MS) {
                _escCalib      = EscCalib::CAL_LOW;
                _escCalibStart = now;
                writeDuty(LEDC_CH_WEAPON_MOTOR, usToDuty(PWM_WEAPON_IDLE_US));
                Serial.println("[Motors] ESC Calibration step 2/2 — minimum throttle (1000 µs).");
            }
            break;

        case EscCalib::CAL_LOW:
            if (now - _escCalibStart >= ESC_CALIB_LOW_MS) {
                _escCalib = EscCalib::DONE;
                writeDuty(LEDC_CH_WEAPON_MOTOR, usToDuty(PWM_WEAPON_IDLE_US));
                Serial.println("[Motors] ESC Full-range calibration complete.");
            }
            break;

        default:
            break;  // IDLE and DONE are sticky — nothing to advance
    }

    // Phase 3 — weapon speed ramp (runs every loop when not calibrating)
    if (!isEscCalibrating()) {
        stepWeaponRamp();
    }
}

bool MotorController::startEscFullCalib() {
    if (!_escReady) {
        Serial.println("[Motors] Cannot calibrate — ESC still arming.");
        return false;
    }
    if (_escCalib == EscCalib::CAL_HIGH || _escCalib == EscCalib::CAL_LOW) {
        Serial.println("[Motors] Cannot start calibration — already in progress.");
        return false;
    }
    _weaponActive  = false;
    _escCalib      = EscCalib::CAL_HIGH;
    _escCalibStart = millis();
    writeDuty(LEDC_CH_WEAPON_MOTOR, usToDuty(PWM_WEAPON_FULL_US));
    Serial.println("[Motors] ESC Full-range calibration started: step 1/2 — maximum throttle (2000 µs).");
    return true;
}

const char* MotorController::escCalibStr() const {
    switch (_escCalib) {
        case EscCalib::CAL_HIGH: return "high";
        case EscCalib::CAL_LOW:  return "low";
        case EscCalib::DONE:     return "done";
        default:                 return "idle";
    }
}

// ── Weapon speed ramp & acceleration ─────────────────────────────────────────

void MotorController::applyWeaponPwm() {
    // Apply the output curve to _weaponCurrentSpeed, then map to PWM pulse width.
    //   Linear    : output = t
    //   Quadratic : output = t²         (slow start, fast finish)
    //   S-Curve   : output = 3t² - 2t³  (smoothstep — eases in and out)
    float t = _weaponCurrentSpeed;
    float curved;
    switch (_weaponCurve) {
        case 1:  curved = t * t;                      break;  // quadratic
        case 2:  curved = t * t * (3.0f - 2.0f * t); break;  // smoothstep
        default: curved = t;                          break;  // linear
    }
    uint32_t pulse_us = (uint32_t)(PWM_WEAPON_IDLE_US +
                        curved * (float)(PWM_WEAPON_FULL_US - PWM_WEAPON_IDLE_US));
    writeDuty(LEDC_CH_WEAPON_MOTOR, usToDuty(pulse_us));
}

void MotorController::stepWeaponRamp() {
    uint32_t now = millis();
    float    dt  = (float)(now - _lastRampTick_ms) / 1000.0f;
    _lastRampTick_ms = now;  // always update — prevents dt spike after idle periods

    if (_weaponCurrentSpeed == _weaponTargetSpeed) return;  // already there

    // Cap dt to one expected loop tick so a long idle doesn't cause an instant jump
    if (dt > 0.05f) dt = 0.05f;

    float maxStep = (_weaponRampMs > 0)
                    ? dt * (1000.0f / (float)_weaponRampMs)   // ramp rate: 1 unit per rampMs ms
                    : 1.0f;                                    // instant: jump in one step

    float diff = _weaponTargetSpeed - _weaponCurrentSpeed;
    if (fabsf(diff) <= maxStep) {
        _weaponCurrentSpeed = _weaponTargetSpeed;             // close enough — snap to target
    } else {
        _weaponCurrentSpeed += (diff > 0.0f ? maxStep : -maxStep);
    }
    applyWeaponPwm();
}

void MotorController::setWeaponRamp(uint16_t rampMs) {
    _weaponRampMs = rampMs;
    _weaponPrefs.begin("robot_weapon", false);
    _weaponPrefs.putUShort("ramp_ms", _weaponRampMs);
    _weaponPrefs.end();
    Serial.printf("[Motors] Weapon ramp set to %u ms.\n", _weaponRampMs);
}

void MotorController::setWeaponCurve(uint8_t curve) {
    if (curve > 2) curve = 0;
    _weaponCurve = curve;
    _weaponPrefs.begin("robot_weapon", false);
    _weaponPrefs.putUChar("curve", _weaponCurve);
    _weaponPrefs.end();
    Serial.printf("[Motors] Weapon curve set to %u.\n", _weaponCurve);
}
