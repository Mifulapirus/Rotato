// =============================================================================
// BLEManager.cpp — Bluetooth Low Energy Stub (Implementation)
// =============================================================================
//
// This file intentionally only has stub implementations.
// See BLEManager.h for the full explanation of the planned BLE architecture.
//
// TO IMPLEMENT BLE:
//   1. #define BLE_ENABLED in config.h
//   2. Fill in the begin() method with NimBLE server/service/characteristic setup
//   3. Fill in update() to read written values and trigger callbacks
//   4. Fill in sendStatus() to notify the STATUS characteristic
// =============================================================================

#include "BLEManager.h"

void BLEManager::begin() {
#ifdef BLE_ENABLED
    // TODO: Initialize NimBLE device with robot name
    // NimBLEDevice::init("Rotato");
    // _server = NimBLEDevice::createServer();
    // ... create service, add characteristics, start advertising
    Serial.println("[BLE] BLE_ENABLED is defined but not yet implemented.");
#else
    // BLE is disabled — nothing to do
#endif
}

void BLEManager::update() {
#ifdef BLE_ENABLED
    // TODO: Read DRIVE_X, DRIVE_Y, WEAPON characteristics
    // and call _driveCallback / _weaponCallback when values change
#endif
}

bool BLEManager::isConnected() const {
#ifdef BLE_ENABLED
    // TODO: return NimBLEDevice::getServer()->getConnectedCount() > 0;
    return false;
#else
    return false;
#endif
}

void BLEManager::sendStatus(uint8_t batteryPercent, bool safetyOk, bool weaponActive) {
#ifdef BLE_ENABLED
    if (!isConnected()) return;
    // TODO: Build a status string and notify _statusChar
    // char buf[32];
    // snprintf(buf, sizeof(buf), "%d,%d,%d", batteryPercent, safetyOk, weaponActive);
    // _statusChar->setValue(buf);
    // _statusChar->notify();
#endif
}
