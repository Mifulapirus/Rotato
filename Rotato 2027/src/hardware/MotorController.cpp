// =============================================================================
// MotorController.cpp — Drive and Weapon Motor Control (Implementation)
// =============================================================================
// Copyright (c) 2026 Angel Hernandez (www.thehomelab.dev)
// Non-Commercial Source-Available License — see LICENSE for full terms.
// Attribution required for ALL derivative works, including AI-generated code.

#include "MotorController.h"

// ── Private helpers ──────────────────────────────────────────────────────────

uint32_t MotorController::usToDuty(uint32_t pulse_us) {
    // Convert pulse width (µs) to duty cycle for the weapon LEDC channel.
    // Formula: duty = (pulse_us / period_us) × (2^resolution - 1)
    return (uint32_t)((float)pulse_us / (float)PWM_PERIOD_US * (float)((1u << PWM_RESOLUTION_BITS) - 1));
}

void MotorController::writeDriveMotor(uint8_t chIn1, uint8_t chIn2, float speed, bool reversed) {
    // Clamp speed to valid range
    if (speed >  1.0f) speed =  1.0f;
    if (speed < -1.0f) speed = -1.0f;

    // Optionally invert for mirrored motors (see MOTOR_LEFT/RIGHT_REVERSED in config.h)
    if (reversed) speed = -speed;

    const uint32_t maxDuty = (1u << DRIVE_PWM_BITS) - 1;
    uint32_t duty = (uint32_t)(fabsf(speed) * (float)DRIVE_MAX_POWER / 100.0f * (float)maxDuty + 0.5f);
    if (duty > maxDuty) duty = maxDuty;

#if DRIVE_SLOW_DECAY
    // Slow decay: hold one input HIGH and PWM the other with inverted duty.
    //   forward: IN1 = 1, IN2 = PWM(1 - d)     reverse: IN1 = PWM(1 - d), IN2 = 1
    //   stop   : IN1 = IN2 = 1 → brake
    if (speed >= 0.0f) {
        ledcWrite(chIn1, maxDuty);
        ledcWrite(chIn2, maxDuty - duty);
    } else {
        ledcWrite(chIn1, maxDuty - duty);
        ledcWrite(chIn2, maxDuty);
    }
#else
    // Fast decay: PWM one input, hold the other LOW.
    //   forward: IN1 = PWM(d), IN2 = 0         reverse: IN1 = 0, IN2 = PWM(d)
    //   stop   : IN1 = IN2 = 0 → coast
    if (speed >= 0.0f) {
        ledcWrite(chIn1, duty);
        ledcWrite(chIn2, 0);
    } else {
        ledcWrite(chIn1, 0);
        ledcWrite(chIn2, duty);
    }
#endif
}

// ── Public methods ────────────────────────────────────────────────────────────

void MotorController::begin() {
    {
        Preferences p;
        if (p.begin("robot_drive", true)) {          // fails if never saved yet
            _driveReversed = p.getBool("reversed", false);
            p.end();
        }
        if (_driveReversed) Serial.println("[Motors] Motors direction inverted (setting).");
    }
    // Weapon acceleration saved by setWeaponRamp() / setWeaponCurve()
    if (_weaponPrefs.begin("robot_weapon", true)) {  // fails if never saved yet
        _weaponRampMs = _weaponPrefs.getUShort("ramp_ms", WEAPON_RAMP_MS_DEFAULT);
        _weaponCurve  = _weaponPrefs.getUChar("curve", WEAPON_CURVE_DEFAULT);
        _weaponPrefs.end();
        if (_weaponCurve > 2) _weaponCurve = WEAPON_CURVE_DEFAULT;
    }
    Serial.printf("[Motors] Weapon ramp %u ms, curve %u.\n", _weaponRampMs, _weaponCurve);


    // Set up LEDC channels and attach them to their GPIO pins.
    // The LEDC peripheral is ESP32's hardware PWM generator.

    // Drive: four 20 kHz channels, one per AT8236 input (AIN1/AIN2/BIN1/BIN2)
    ledcSetup(LEDC_CH_LEFT_IN1,  DRIVE_PWM_FREQ_HZ, DRIVE_PWM_BITS);
    ledcSetup(LEDC_CH_LEFT_IN2,  DRIVE_PWM_FREQ_HZ, DRIVE_PWM_BITS);
    ledcSetup(LEDC_CH_RIGHT_IN1, DRIVE_PWM_FREQ_HZ, DRIVE_PWM_BITS);
    ledcSetup(LEDC_CH_RIGHT_IN2, DRIVE_PWM_FREQ_HZ, DRIVE_PWM_BITS);
    ledcAttachPin(PIN_LEFT_IN1,  LEDC_CH_LEFT_IN1);
    ledcAttachPin(PIN_LEFT_IN2,  LEDC_CH_LEFT_IN2);
    ledcAttachPin(PIN_RIGHT_IN1, LEDC_CH_RIGHT_IN1);
    ledcAttachPin(PIN_RIGHT_IN2, LEDC_CH_RIGHT_IN2);

    // Weapon: 50 Hz RC ESC signal
    ledcSetup(LEDC_CH_WEAPON_MOTOR, PWM_FREQUENCY_HZ, PWM_RESOLUTION_BITS);
    ledcAttachPin(PIN_MOTOR_WEAPON, LEDC_CH_WEAPON_MOTOR);

    // Drive motors start stopped
    writeDriveMotor(LEDC_CH_LEFT_IN1,  LEDC_CH_LEFT_IN2,  0.0f, false);
    writeDriveMotor(LEDC_CH_RIGHT_IN1, LEDC_CH_RIGHT_IN2, 0.0f, false);

    // ── Weapon ESC arming ────────────────────────────────────────────────────
    // RC ESCs require the PWM signal to be held at minimum throttle (1000 µs)
    // at power-on before they will accept commands. We hold this for
    // ESC_INIT_DELAY_MS and block all weapon commands until ready.
    _escReady     = false;
    _escInitStart = millis();
    ledcWrite(LEDC_CH_WEAPON_MOTOR, usToDuty(PWM_WEAPON_IDLE_US));

    Serial.printf("[Motors] Weapon ESC arming — holding %u µs for %u ms.\n",
                  PWM_WEAPON_IDLE_US, ESC_INIT_DELAY_MS);
    Serial.printf("[Motors] AT8236 drive initialized (%u Hz, %s decay).\n",
                  DRIVE_PWM_FREQ_HZ, DRIVE_SLOW_DECAY ? "slow" : "fast");
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

    // "Invert Motors Direction": the back becomes the front. Flipping only Y
    // keeps turns matching the stick (spin right is still clockwise).
    // DRIVE_FORWARD_FLIPPED (config.h) sets the default front.
    if (_driveReversed != DRIVE_FORWARD_FLIPPED) y = -y;

    float leftSpeed  = y + x;
    float rightSpeed = y - x;

    // Clamp to [-1.0, 1.0] — values over 1.0 can occur from the mixing
    if (leftSpeed  >  1.0f) leftSpeed  =  1.0f;
    if (leftSpeed  < -1.0f) leftSpeed  = -1.0f;
    if (rightSpeed >  1.0f) rightSpeed =  1.0f;
    if (rightSpeed < -1.0f) rightSpeed = -1.0f;

    writeDriveMotor(LEDC_CH_LEFT_IN1,  LEDC_CH_LEFT_IN2,  leftSpeed,  MOTOR_LEFT_REVERSED);
    writeDriveMotor(LEDC_CH_RIGHT_IN1, LEDC_CH_RIGHT_IN2, rightSpeed, MOTOR_RIGHT_REVERSED);

    Serial.printf("[Motors] Drive x=%.2f y=%.2f -> L=%.2f R=%.2f\n",
                  x, y, leftSpeed, rightSpeed);
}

void MotorController::setDriveReversed(bool reversed) {
    _driveReversed = reversed;
    Preferences p;
    if (p.begin("robot_drive", false)) {
        p.putBool("reversed", reversed);
        p.end();
    }
    Serial.printf("[Motors] Motors direction %s.\n", reversed ? "inverted" : "normal");
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
    // Stop both drive motors and send idle (1000µs) to the weapon ESC.
    // Immediately kills ramp and calibration.
    writeDriveMotor(LEDC_CH_LEFT_IN1,  LEDC_CH_LEFT_IN2,  0.0f, false);
    writeDriveMotor(LEDC_CH_RIGHT_IN1, LEDC_CH_RIGHT_IN2, 0.0f, false);
    ledcWrite(LEDC_CH_WEAPON_MOTOR, usToDuty(PWM_WEAPON_IDLE_US));
    _weaponActive       = false;
    _weaponTargetSpeed  = 0.0f;
    _weaponCurrentSpeed = 0.0f;
    if (_escCalib == EscCalib::CAL_HIGH || _escCalib == EscCalib::CAL_LOW) {
        _escCalib = EscCalib::IDLE;
        Serial.println("[Motors] ESC calibration aborted by stopAll.");
    }
    Serial.println("[Motors] All stopped.");
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
                ledcWrite(LEDC_CH_WEAPON_MOTOR, usToDuty(PWM_WEAPON_IDLE_US));
                Serial.println("[Motors] ESC Calibration step 2/2 — minimum throttle (1000 µs).");
            }
            break;

        case EscCalib::CAL_LOW:
            if (now - _escCalibStart >= ESC_CALIB_LOW_MS) {
                _escCalib = EscCalib::DONE;
                ledcWrite(LEDC_CH_WEAPON_MOTOR, usToDuty(PWM_WEAPON_IDLE_US));
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
    ledcWrite(LEDC_CH_WEAPON_MOTOR, usToDuty(PWM_WEAPON_FULL_US));
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
    ledcWrite(LEDC_CH_WEAPON_MOTOR, usToDuty(pulse_us));
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
