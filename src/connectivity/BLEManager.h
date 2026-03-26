#pragma once
// =============================================================================
// BLEManager.h — Bluetooth Low Energy (Stub for Future Android App)
// =============================================================================
//
// This is a STUB — a placeholder that doesn't do anything yet.
// It's here so the architecture is ready for BLE when you want to add it.
//
// WHAT IS BLE?
//   Bluetooth Low Energy (BLE) is a wireless protocol for low-power devices.
//   Unlike classic Bluetooth, BLE uses a "GATT" (Generic Attribute Profile)
//   structure to organize data.
//
// GATT CONCEPTS (for Android app development later):
//   - SERVICE: A group of related data (like "Robot Control Service")
//   - CHARACTERISTIC: A single piece of data within a service
//     (like "left motor speed" or "battery level")
//   - CLIENT: The Android phone that connects to the robot
//   - SERVER: The ESP32 that advertises data
//
// PLANNED CHARACTERISTICS:
//   UUID: 4 hex groups, e.g. "12345678-1234-1234-1234-123456789abc"
//
//   Service:  ROBOT_SERVICE_UUID
//     char: DRIVE_X_UUID       — float, write (joystick X axis)
//     char: DRIVE_Y_UUID       — float, write (joystick Y axis)
//     char: WEAPON_UUID        — bool,  write (weapon on/off)
//     char: STATUS_UUID        — string, notify (battery%, safety, IP)
//
// TO ENABLE:
//   1. Uncomment #define BLE_ENABLED in config.h
//   2. Fill in the method bodies below using the NimBLE-Arduino library
//   3. Call bleManager.begin() and bleManager.update() from main.cpp
//
// NOTES FOR ANDROID DEVELOPMENT:
//   - Use Android's BluetoothLeScanner to find the robot by service UUID
//   - Connect and discover the service/characteristics
//   - Write float bytes to DRIVE_X and DRIVE_Y characteristics
//   - Subscribe to STATUS_UUID notifications for live battery/status updates
// =============================================================================

#include <Arduino.h>
#include "config.h"

#ifdef BLE_ENABLED
#include <NimBLEDevice.h>

// ── GATT UUIDs ──────────────────────────────────────────────────────────────
// These unique IDs identify the robot's BLE service and characteristics.
// The Android app will use these same UUIDs to find and talk to the robot.
#define ROBOT_SERVICE_UUID  "a1b2c3d4-e5f6-7890-abcd-ef1234567890"
#define DRIVE_X_UUID        "a1b2c3d4-e5f6-7890-abcd-ef1234567891"
#define DRIVE_Y_UUID        "a1b2c3d4-e5f6-7890-abcd-ef1234567892"
#define WEAPON_UUID         "a1b2c3d4-e5f6-7890-abcd-ef1234567893"
#define STATUS_UUID         "a1b2c3d4-e5f6-7890-abcd-ef1234567894"

#endif // BLE_ENABLED

class BLEManager {
public:
    // Initialize BLE device, create service and characteristics.
    // Start advertising so Android phones can discover the robot.
    void begin();

    // Check for incoming BLE commands and send status notifications.
    // Call from loop().
    void update();

    // Returns true if an Android device is currently connected via BLE.
    bool isConnected() const;

    // Register callbacks (same pattern as WebServerManager)
    using DriveCallback  = std::function<void(float x, float y)>;
    using WeaponCallback = std::function<void(bool active)>;
    void onDrive(DriveCallback cb)   { _driveCallback = cb; }
    void onWeapon(WeaponCallback cb) { _weaponCallback = cb; }

    // Send status update to connected BLE client (battery%, safety, weapon state)
    void sendStatus(uint8_t batteryPercent, bool safetyOk, bool weaponActive);

private:
    DriveCallback  _driveCallback;
    WeaponCallback _weaponCallback;

#ifdef BLE_ENABLED
    NimBLEServer*         _server         = nullptr;
    NimBLECharacteristic* _driveXChar     = nullptr;
    NimBLECharacteristic* _driveYChar     = nullptr;
    NimBLECharacteristic* _weaponChar     = nullptr;
    NimBLECharacteristic* _statusChar     = nullptr;
#endif
};
