// =============================================================================
// BLEManager.cpp — Bluetooth Low Energy GATT Server (Implementation)
// =============================================================================
// Copyright (c) 2026 Angel Hernandez (www.thehomelab.dev)
// Non-Commercial Source-Available License — see LICENSE for full terms.
// Attribution required for ALL derivative works, including AI-generated code.

#include "BLEManager.h"

#ifdef BLE_ENABLED

#include <ArduinoJson.h>
#include "version_build.h"

// ── Singleton pointer ─────────────────────────────────────────────────────────
// NimBLE callbacks are C++ objects without a built-in "this" context.
// We store the one BLEManager instance here so the callbacks can reach it.
static BLEManager* s_instance = nullptr;

// =============================================================================
// Server callbacks — connection/disconnection events
// =============================================================================
class RobotServerCB : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) override {
        Serial.printf("[BLE] Client connected: %s\n",
                      connInfo.getAddress().toString().c_str());
        // Request tighter connection parameters for lower control latency.
        // 12–24 × 1.25 ms = 15–30 ms interval; 0 latency; 2 s supervision timeout.
        pServer->updateConnParams(connInfo.getConnHandle(), 12, 24, 0, 200);
    }

    void onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) override {
        Serial.printf("[BLE] Client disconnected (reason %d) — restarting advertising\n", reason);
        // Safety: zero the drive outputs when the phone disconnects.
        if (s_instance) s_instance->_onDriveWrite(0.0f, 0.0f);
        NimBLEDevice::startAdvertising();
    }

    void onMTUChange(uint16_t MTU, NimBLEConnInfo& connInfo) override {
        Serial.printf("[BLE] MTU updated to %u (conn handle %u)\n",
                      MTU, connInfo.getConnHandle());
    }
};

// =============================================================================
// Characteristic callbacks — called when Android writes a value
// =============================================================================
class RobotCharCB : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* pChar, NimBLEConnInfo& connInfo) override {
        if (!s_instance) return;

        // Compare by pointer — faster than UUID string comparison.
        if (pChar == s_instance->_driveXChar) {
            // 4-byte little-endian IEEE 754 float (matches Android's ByteOrder.LITTLE_ENDIAN)
            float x = pChar->getValue<float>();
            s_instance->_onDriveWrite(x, s_instance->_driveY);

        } else if (pChar == s_instance->_driveYChar) {
            float y = pChar->getValue<float>();
            s_instance->_onDriveWrite(s_instance->_driveX, y);

        } else if (pChar == s_instance->_weaponChar) {
            uint8_t val = pChar->getValue<uint8_t>();
            s_instance->_onWeaponWrite(val != 0);
        }
    }
};

static RobotServerCB s_serverCB;
static RobotCharCB   s_charCB;

#endif // BLE_ENABLED

// =============================================================================
// Public methods
// =============================================================================

void BLEManager::begin(const String& deviceName) {
#ifdef BLE_ENABLED
    s_instance = this;

    // ── Init NimBLE stack ────────────────────────────────────────────────────
    NimBLEDevice::init(deviceName.c_str());
    NimBLEDevice::setPower(9);  // +9 dBm — maximum for ESP32-C3

    // ── GATT server ──────────────────────────────────────────────────────────
    _server = NimBLEDevice::createServer();
    _server->setCallbacks(&s_serverCB);

    // ── Service + characteristics ────────────────────────────────────────────
    NimBLEService* pSvc = _server->createService(ROBOT_SERVICE_UUID);

    // Drive axes — WRITE NO RESPONSE: phone sends, robot acts, no ACK needed.
    // max_len = 4 bytes (IEEE 754 float).
    _driveXChar = pSvc->createCharacteristic(DRIVE_X_UUID, NIMBLE_PROPERTY::WRITE_NR, 4);
    _driveYChar = pSvc->createCharacteristic(DRIVE_Y_UUID, NIMBLE_PROPERTY::WRITE_NR, 4);

    // Weapon — WRITE NO RESPONSE: 1 byte, 0x00=off / 0x01=on.
    _weaponChar = pSvc->createCharacteristic(WEAPON_UUID,  NIMBLE_PROPERTY::WRITE_NR, 1);

    // Status — NOTIFY: robot pushes JSON to subscribed Android client every 500 ms.
    // max_len = 128 bytes (JSON payload is ~60 bytes).
    _statusChar = pSvc->createCharacteristic(STATUS_UUID,  NIMBLE_PROPERTY::NOTIFY,   128);

    _driveXChar->setCallbacks(&s_charCB);
    _driveYChar->setCallbacks(&s_charCB);
    _weaponChar->setCallbacks(&s_charCB);

    // ── Advertising ──────────────────────────────────────────────────────────
    // BLE advertising packets are capped at 31 bytes.
    // A 128-bit service UUID alone takes 17 bytes; "Rotato-9CD9" takes 13 bytes;
    // plus the mandatory 3-byte flags field — that totals 33 bytes and overflows.
    //
    // Solution: split across two packets (both 31 bytes max):
    //   Main advert  → flags + service UUID   (used by Android to scan-filter)
    //   Scan response → device name           (returned on active scan request)
    //
    // Android's BluetoothLeScanner finds the robot by UUID in the main advert;
    // the device name in the scan response is then available as scanResult.device.name.
    NimBLEAdvertising* pAdv = NimBLEDevice::getAdvertising();

    NimBLEAdvertisementData advData;
    advData.setFlags(0x06);                      // LE General Discoverable | BR/EDR Not Supported
    advData.addServiceUUID(ROBOT_SERVICE_UUID);  // 17 bytes — fits with flags (3 bytes) = 20 bytes total

    NimBLEAdvertisementData scanData;
    scanData.setName(deviceName.c_str());        // 13 bytes — fits comfortably in scan response

    pAdv->setAdvertisementData(advData);
    pAdv->setScanResponseData(scanData);
    pAdv->enableScanResponse(true);
    pAdv->start();

    Serial.printf("[BLE] Advertising as \"%s\"\n", deviceName.c_str());
    Serial.printf("[BLE] Service UUID : %s\n", ROBOT_SERVICE_UUID);
#else
    Serial.println("[BLE] Disabled — set #define BLE_ENABLED in config.h to activate.");
#endif
}

void BLEManager::update() {
    // NimBLE is fully event-driven via callbacks.
    // Nothing to poll here; this method exists for API symmetry.
}

bool BLEManager::isConnected() const {
#ifdef BLE_ENABLED
    return _server && (_server->getConnectedCount() > 0);
#else
    return false;
#endif
}

void BLEManager::sendStatus(uint8_t batteryPercent, bool safetyOk, bool weaponActive) {
#ifdef BLE_ENABLED
    if (!isConnected()) return;

    // Build compact JSON — matches the spec in ANDROID_APP_BRIEF.md.
    //   {"battery":85,"safety":true,"weapon":false,"fw":"1.0.0+42"}
    JsonDocument doc;
    doc["battery"] = batteryPercent;
    doc["safety"]  = safetyOk;
    doc["weapon"]  = weaponActive;
    doc["fw"]      = FW_VERSION_FULL;

    char buf[128];
    size_t len = serializeJson(doc, buf, sizeof(buf));

    // notify(const T& s) template matches std::string/c_str pattern;
    // we pass raw bytes directly for predictability.
    _statusChar->notify(reinterpret_cast<const uint8_t*>(buf), len);
#endif
}

// ── Internal bridge methods — called by RobotCharCB ──────────────────────────

void BLEManager::_onDriveWrite(float x, float y) {
    _driveX = x;
    _driveY = y;
    if (!_enabled) return;  // BLE control disabled via settings panel
    if (_driveCallback) _driveCallback(x, y);
}

void BLEManager::_onWeaponWrite(bool active) {
    if (!_enabled) return;  // BLE control disabled via settings panel
    if (_weaponCallback) _weaponCallback(active);
}

void BLEManager::setDeviceName(const String& name) {
#ifdef BLE_ENABLED
    NimBLEAdvertising* pAdv = NimBLEDevice::getAdvertising();
    pAdv->stop();
    NimBLEAdvertisementData scanData;
    scanData.setName(name.c_str());
    pAdv->setScanResponseData(scanData);
    pAdv->start();
    Serial.printf("[BLE] Advertising name updated to \"%s\"\n", name.c_str());
#endif
}

void BLEManager::setEnabled(bool enabled) {
    _enabled = enabled;
    Serial.printf("[BLE] Control %s.\n", enabled ? "enabled" : "disabled");
    if (!enabled) {
        // Immediately zero drive so robot doesn't keep moving on BLE lock-out.
        if (_driveCallback) _driveCallback(0.0f, 0.0f);
#ifdef BLE_ENABLED
        NimBLEDevice::stopAdvertising();
#endif
    } else {
#ifdef BLE_ENABLED
        NimBLEDevice::startAdvertising();
#endif
    }
}
