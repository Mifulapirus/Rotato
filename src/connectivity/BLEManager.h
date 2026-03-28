#pragma once
// =============================================================================
// BLEManager.h — Bluetooth Low Energy GATT Server (Android app transport)
// =============================================================================
//
// Runs a BLE GATT server so the Android app can control the robot without WiFi.
//
// GATT PROFILE:
//   Service:  ROBOT_SERVICE_UUID
//     DRIVE_X  — WRITE_NR  4-byte little-endian float  [-1.0 … +1.0]  joystick X
//     DRIVE_Y  — WRITE_NR  4-byte little-endian float  [-1.0 … +1.0]  joystick Y
//     WEAPON   — WRITE_NR  1-byte uint8  0x00=off / 0x01=on
//     STATUS   — NOTIFY    UTF-8 JSON {"battery":85,"safety":true,"weapon":false,"fw":"1.0.0+1"}
//
// ENABLE:
//   #define BLE_ENABLED in config.h  (already on by default)
//
// USAGE (main.cpp):
//   ble.begin(wifi.getSSID());   // advertise as "Rotato-XXXX"
//   ble.onDrive([](float x, float y){ ... });
//   ble.onWeapon([](bool active){ ... });
//   // loop():
//   ble.sendStatus(battery%, safety, weapon);
//   ble.update();  // light housekeeping
// =============================================================================

#include <Arduino.h>
#include "config.h"

#ifdef BLE_ENABLED
#include <NimBLEDevice.h>

// Forward declarations — implementations live in BLEManager.cpp.
// Declared here so BLEManager can grant them friend access.
class RobotServerCB;
class RobotCharCB;

// ── GATT UUIDs ──────────────────────────────────────────────────────────────
// The Android app must use these same UUIDs to discover and talk to the robot.
#define ROBOT_SERVICE_UUID  "a1b2c3d4-e5f6-7890-abcd-ef1234567890"
#define DRIVE_X_UUID        "a1b2c3d4-e5f6-7890-abcd-ef1234567891"
#define DRIVE_Y_UUID        "a1b2c3d4-e5f6-7890-abcd-ef1234567892"
#define WEAPON_UUID         "a1b2c3d4-e5f6-7890-abcd-ef1234567893"
#define STATUS_UUID         "a1b2c3d4-e5f6-7890-abcd-ef1234567894"

#endif // BLE_ENABLED

class BLEManager {
public:
    // Start BLE advertising.  deviceName should match the WiFi SSID ("Rotato-XXXX").
    void begin(const String& deviceName = "Rotato");

    // Housekeeping — call from loop() (currently a no-op; NimBLE is event-driven).
    void update();

    // Enable or disable BLE command acceptance at runtime (no reboot needed).
    // When disabled: BLE drive/weapon commands are ignored and motion is zeroed.
    void setEnabled(bool enabled);

    // Update the BLE advertising / scan-response device name at runtime.
    // Stops advertising, swaps the name, then restarts.  No reboot needed.
    void setDeviceName(const String& name);

    // Returns true when an Android device is connected.
    bool isConnected() const;

    // Register callbacks — same pattern as WebServerManager.
    using DriveCallback  = std::function<void(float x, float y)>;
    using WeaponCallback = std::function<void(bool active)>;
    void onDrive(DriveCallback cb)   { _driveCallback = cb; }
    void onWeapon(WeaponCallback cb) { _weaponCallback = cb; }

    // Notify the connected Android client with the current robot status.
    // Called from main.cpp every WS_STATUS_INTERVAL_MS.
    void sendStatus(uint8_t batteryPercent, bool safetyOk, bool weaponActive);

    // ── Called by BLE characteristic callbacks (not for external use) ────────
    // Prefixed with _ to signal these are internal bridge methods.
    void _onDriveWrite(float x, float y);
    void _onWeaponWrite(bool active);

private:
    bool           _enabled = true;   // runtime BLE enable/disable switch
    DriveCallback  _driveCallback;
    WeaponCallback _weaponCallback;

    // Last known drive values — cached so both axes fire the callback together.
    float _driveX = 0.0f;
    float _driveY = 0.0f;

#ifdef BLE_ENABLED
    friend class RobotServerCB;  // needs _driveCallback on disconnect
    friend class RobotCharCB;    // needs _driveXChar/_driveYChar/_weaponChar and cached axes

    NimBLEServer*         _server     = nullptr;
    NimBLECharacteristic* _driveXChar = nullptr;
    NimBLECharacteristic* _driveYChar = nullptr;
    NimBLECharacteristic* _weaponChar = nullptr;
    NimBLECharacteristic* _statusChar = nullptr;
#endif
};
