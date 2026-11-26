// =============================================================================
// GamepadManager.cpp — Bluetooth LE game controller (Implementation)
// =============================================================================
// Copyright (c) 2026 Angel Hernandez (www.thehomelab.dev)
// Non-Commercial Source-Available License — see LICENSE for full terms.
// Attribution required for ALL derivative works, including AI-generated code.

#include "GamepadManager.h"
#include "BtNearby.h"

// Small helpers shared by both the enabled and disabled builds.
static float applyDeadzone(float v, float dz) {
    float a = fabsf(v);
    if (a <= dz) return 0.0f;
    float scaled = (a - dz) / (1.0f - dz);       // rescale so output still reaches 1.0
    if (scaled > 1.0f) scaled = 1.0f;
    return v < 0 ? -scaled : scaled;
}

#ifdef GAMEPAD_ENABLED

#include <NimBLEDevice.h>

static GamepadManager* s_instance = nullptr;

// HID-over-GATT UUIDs (Bluetooth SIG assigned numbers)
static const NimBLEUUID HID_SERVICE_UUID((uint16_t)0x1812);
static const NimBLEUUID HID_REPORT_UUID ((uint16_t)0x2A4D);

static constexpr size_t   XBOX_REPORT_LEN      = 16;
static constexpr uint32_t SCAN_PERIOD_MS       = 5000;   // restart scans so the duplicate filter resets
static constexpr uint32_t CONNECT_TIMEOUT_MS   = 5000;
static constexpr uint32_t RETRY_BACKOFF_MS     = 15000;  // ignore a device this long after a failed attempt
static constexpr uint32_t TASK_PERIOD_MS       = 50;
static constexpr uint32_t LINK_INFO_PERIOD_MS  = 2000;   // RSSI / link params / report rate refresh
static constexpr uint32_t BATTERY_PERIOD_MS    = 60000;  // battery level re-read

// ── State owned by the gamepad task / NimBLE callbacks ──────────────────────
static NimBLEClient*  s_client        = nullptr;
static NimBLEAddress  s_pendingAddr;
static volatile bool  s_havePending   = false;
static NimBLEAddress  s_failedAddr;                  // last device that failed to connect
static uint32_t       s_failed_ms     = 0;
static uint32_t       s_bootPairUntil = 0;           // boot pairing window end

// =============================================================================
// NimBLE callbacks
// =============================================================================
class GamepadScanCB : public NimBLEScanCallbacks {
    void onResult(const NimBLEAdvertisedDevice* dev) override {
        if (s_instance) s_instance->_onAdvertisement(dev);
    }
};

class GamepadClientCB : public NimBLEClientCallbacks {
    void onDisconnect(NimBLEClient* client, int reason) override {
        if (s_instance) s_instance->_onDisconnected(reason);
    }
};

static GamepadScanCB   s_scanCB;
static GamepadClientCB s_clientCB;

static void notifyCB(NimBLERemoteCharacteristic*, uint8_t* data, size_t len, bool) {
    if (s_instance) s_instance->_onReport(data, len);
}

// =============================================================================
// Public API
// =============================================================================

void GamepadManager::begin() {
    s_instance = this;

    if (!NimBLEDevice::isInitialized()) {
        NimBLEDevice::init("");
    }
    // Xbox controllers require an encrypted, bonded link. The ESP32 has no
    // display/keyboard, so pairing uses "Just Works" (no PIN).
    NimBLEDevice::setSecurityAuth(true /*bond*/, false /*mitm*/, true /*secure connections*/);
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);

    s_bootPairUntil = millis() + GAMEPAD_BOOT_PAIR_WINDOW_MS;
    _phase = Phase::SEARCHING;

    xTaskCreatePinnedToCore(taskEntry, "gamepad", 8192, this, 1, nullptr, 0);

    Serial.printf("[Pad] Gamepad support on — %d paired controller(s). "
                  "New controllers accepted for %u s.\n",
                  NimBLEDevice::getNumBonds(), (unsigned)(GAMEPAD_BOOT_PAIR_WINDOW_MS / 1000));
}

void GamepadManager::startPairing() {
    uint32_t until = millis() + GAMEPAD_PAIR_WINDOW_MS;
    _pairUntil_ms = until ? until : 1;
    Serial.printf("[Pad] Pairing window open for %u s — hold the controller's pair button.\n",
                  (unsigned)(GAMEPAD_PAIR_WINDOW_MS / 1000));
}

void GamepadManager::forgetAll() {
    _forgetRequest = true;   // handled by the gamepad task
}

bool GamepadManager::isPairingRequested() const {
    uint32_t until = _pairUntil_ms;
    return until != 0 && (int32_t)(until - millis()) > 0;
}

bool GamepadManager::acceptingNewControllers() const {
    return isPairingRequested()
        || (int32_t)(s_bootPairUntil - millis()) > 0
        || NimBLEDevice::getNumBonds() == 0;
}

const char* GamepadManager::statusStr() const {
    switch (_phase) {
        case Phase::INACTIVE:   return "disabled";
        case Phase::CONNECTING: return "connecting";
        case Phase::CONNECTED:  return "connected";
        case Phase::SEARCHING:
        default:                return isPairingRequested() ? "pairing" : "searching";
    }
}

String GamepadManager::getName() const {
    return _connected ? String(_name) : String();
}

bool GamepadManager::getDeviceInfo(BtDeviceInfo& out) {
    portENTER_CRITICAL(&_infoMux);
    bool have = _haveInfo;
    if (have) out = _info;
    portEXIT_CRITICAL(&_infoMux);
    return have;
}

void GamepadManager::publishInfo(const BtDeviceInfo& info) {
    portENTER_CRITICAL(&_infoMux);
    _info     = info;
    _haveInfo = true;
    portEXIT_CRITICAL(&_infoMux);
}

// =============================================================================
// Loop-task side
// =============================================================================

void GamepadManager::update() {
    bool conn = _connected;
    if (conn != _wasConnected) {
        _wasConnected = conn;
        if (conn) {
            _weaponLockout = true;     // RT must be released once before the weapon responds
            _lastX = _lastY = 0;
            _lastWeapon = 0;
            _seenSeq = _reportSeq;
            _lastState = State();
            _prevButtons = 0;
            _uiDirty = true;
            Serial.printf("[Pad] Controller connected: %s\n", _name);
        } else {
            Serial.println("[Pad] Controller disconnected — stopping motors.");
            stopOutputs();
        }
        if (_connCallback) _connCallback(conn);
    }
    if (!conn) return;

    uint32_t seq = _reportSeq;
    if (seq == _seenSeq) return;   // no new input
    _seenSeq = seq;

    State s;
    portENTER_CRITICAL(&_mux);
    s = _shared;
    portEXIT_CRITICAL(&_mux);
    applyState(s);
}

void GamepadManager::applyState(const State& s) {
    _lastState = s;
    _uiDirty   = true;

    // ── Button presses (edge-triggered) ──────────────────────────────────────
    uint32_t pressed = s.buttons & ~_prevButtons;
    _prevButtons = s.buttons;
    if (pressed && _buttonCallback) _buttonCallback(pressed);

    // ── Drive ────────────────────────────────────────────────────────────────
    float y = applyDeadzone(s.ly, GAMEPAD_STICK_DEADZONE);
    float x = applyDeadzone(GAMEPAD_SPLIT_STICKS ? s.rx : s.lx, GAMEPAD_STICK_DEADZONE);

    if (x == 0.0f && y == 0.0f && s.dpad != 0) {
        // Hat: 1 = up, then clockwise in 45° steps
        static const int8_t HAT_X[9] = { 0,  0,  1, 1, 1, 0, -1, -1, -1 };
        static const int8_t HAT_Y[9] = { 0,  1,  1, 0,-1,-1, -1,  0,  1 };
        uint8_t h = s.dpad <= 8 ? s.dpad : 0;
        x = HAT_X[h] * GAMEPAD_DPAD_SPEED;
        y = HAT_Y[h] * GAMEPAD_DPAD_SPEED;
    }
    if (s.buttons & BTN_LB) {
        x *= GAMEPAD_PRECISION_SCALE;
        y *= GAMEPAD_PRECISION_SCALE;
    }

    bool centered    = (x == 0.0f && y == 0.0f);
    bool wasCentered = (_lastX == 0.0f && _lastY == 0.0f);
    if ((centered && !wasCentered) ||
        (!centered && (fabsf(x - _lastX) > 0.01f || fabsf(y - _lastY) > 0.01f))) {
        _lastX = x;
        _lastY = y;
        if (_driveCallback) _driveCallback(x, y);
    }

    // ── Weapon (RT dead-man throttle) ────────────────────────────────────────
    float w = applyDeadzone(s.rt, GAMEPAD_TRIGGER_DEADZONE);
    w = roundf(w * 50.0f) / 50.0f;          // 2 % steps — avoids flooding on tiny changes

    if (s.buttons & BTN_B) {
        _weaponLockout = true;              // B = weapon stop until RT is released
    }
    if (w == 0.0f) {
        _weaponLockout = false;
    }
    if (_weaponLockout) w = 0.0f;

    if (w != _lastWeapon) {
        _lastWeapon = w;
        if (_weaponCallback) _weaponCallback(w);
    }
}

bool GamepadManager::takeUiUpdate(char* buf, size_t len) {
    if (!_uiDirty || !_connected) return false;
    _uiDirty = false;
    const State& s = _lastState;
    char hex[sizeof(s.raw) * 2 + 1];
    for (uint8_t i = 0; i < s.rawLen; i++) sprintf(hex + i * 2, "%02x", s.raw[i]);
    hex[s.rawLen * 2] = 0;
    int n = snprintf(buf, len,
             "{\"type\":\"gamepad\",\"lx\":%.2f,\"ly\":%.2f,\"rx\":%.2f,\"ry\":%.2f,"
             "\"lt\":%.2f,\"rt\":%.2f,\"dpad\":%u,\"btn\":%lu,"
             "\"x\":%.2f,\"y\":%.2f,\"w\":%.2f,\"lock\":%d,\"split\":%d,\"raw\":\"%s\"}",
             s.lx, s.ly, s.rx, s.ry, s.lt, s.rt, (unsigned)s.dpad, (unsigned long)s.buttons,
             _lastX, _lastY, _lastWeapon, _weaponLockout ? 1 : 0, GAMEPAD_SPLIT_STICKS ? 1 : 0, hex);
    return n > 0 && (size_t)n < len;   // never send a truncated (invalid) JSON
}

void GamepadManager::stopOutputs() {
    if ((_lastX != 0.0f || _lastY != 0.0f) && _driveCallback) _driveCallback(0.0f, 0.0f);
    if (_lastWeapon != 0.0f && _weaponCallback) _weaponCallback(0.0f);
    _lastX = _lastY = 0;
    _lastWeapon = 0;
}

// =============================================================================
// NimBLE-task side (keep short — runs on the BLE host task)
// =============================================================================

void GamepadManager::_onReport(const uint8_t* d, size_t len) {
    if (len < XBOX_REPORT_LEN) return;    // other HID reports (e.g. battery) — ignore

    auto u16    = [&](int i) { return (uint16_t)(d[i] | (d[i + 1] << 8)); };
    auto stick  = [](uint16_t v) { float f = ((int32_t)v - 32768) / 32767.0f; return f < -1 ? -1.0f : (f > 1 ? 1.0f : f); };
    auto trig   = [](uint16_t v) { float f = v / 1023.0f; return f > 1 ? 1.0f : f; };

    State s;
    s.lx   =  stick(u16(0));
    s.ly   = -stick(u16(2));               // controller reports 0 = up; we want up = +1
    s.rx   =  stick(u16(4));
    s.ry   = -stick(u16(6));
    s.lt   =  trig(u16(8));
    s.rt   =  trig(u16(10));
    s.dpad =  d[12];

    uint32_t b = 0;
    if (d[13] & 0x01) b |= BTN_A;
    if (d[13] & 0x02) b |= BTN_B;
    if (d[13] & 0x08) b |= BTN_X;
    if (d[13] & 0x10) b |= BTN_Y;
    if (d[13] & 0x40) b |= BTN_LB;
    if (d[13] & 0x80) b |= BTN_RB;
    if (d[14] & 0x04) b |= BTN_VIEW;
    if (d[14] & 0x08) b |= BTN_MENU;
    if (d[14] & 0x10) b |= BTN_XBOX;
    if (d[14] & 0x20) b |= BTN_LS;
    if (d[14] & 0x40) b |= BTN_RS;
    if (d[15] & 0x01) b |= BTN_SHARE;
    s.buttons = b;
    s.rawLen  = (uint8_t)min(len, sizeof(s.raw));
    memcpy(s.raw, d, s.rawLen);

    portENTER_CRITICAL(&_mux);
    _shared = s;
    portEXIT_CRITICAL(&_mux);
    _reportSeq = _reportSeq + 1;
}

void GamepadManager::_onDisconnected(int reason) {
    Serial.printf("[Pad] Link closed (reason 0x%X: %s)\n", reason, BtInfo::disconnectReason(reason));
    _dropReason = reason ? reason : -1;   // the gamepad task records it in the device info
    _connected = false;
    if (_phase != Phase::INACTIVE) _phase = Phase::SEARCHING;
}

void GamepadManager::_onAdvertisement(const void* p) {
    auto* dev = static_cast<const NimBLEAdvertisedDevice*>(p);
    BtNearby::record(dev);                 // Bluetooth tab "Nearby devices" list (no-op unless scanning for it)
    if (s_havePending || _phase != Phase::SEARCHING) return;
    if (!dev->isConnectable()) return;

    const NimBLEAddress& addr = dev->getAddress();
    if (addr == s_failedAddr && millis() - s_failed_ms < RETRY_BACKOFF_MS) return;

    bool known = NimBLEDevice::isBonded(addr);
    bool isPad = dev->getName().find(GAMEPAD_NAME_FILTER) != std::string::npos;
    if (!known && !(isPad && acceptingNewControllers())) return;

    Serial.printf("[Pad] Found %s \"%s\" (%s)\n", known ? "paired controller" : "new controller",
                  dev->getName().c_str(), addr.toString().c_str());
    s_pendingAddr = addr;
    strlcpy(_name, dev->getName().empty() ? "Controller" : dev->getName().c_str(), sizeof(_name));
    s_havePending = true;
    NimBLEDevice::getScan()->stop();
}

// =============================================================================
// Gamepad task — scanning and the (blocking) connect / pair / subscribe sequence
// =============================================================================

void GamepadManager::taskEntry(void* arg) {
    static_cast<GamepadManager*>(arg)->taskLoop();
}

static void markFailed(const NimBLEAddress& addr) {
    s_failedAddr = addr;
    s_failed_ms  = millis();
}

void GamepadManager::taskLoop() {
    NimBLEScan* scan = NimBLEDevice::getScan();
    scan->setScanCallbacks(&s_scanCB, false);
    scan->setActiveScan(true);             // needed to receive the controller's name
    scan->setInterval(GAMEPAD_SCAN_INTERVAL_MS);
    scan->setWindow(GAMEPAD_SCAN_WINDOW_MS);
    scan->setMaxResults(0);                // don't store results — we act in the callback

    s_client = NimBLEDevice::createClient();
    s_client->setClientCallbacks(&s_clientCB, false);
    s_client->setConnectTimeout(CONNECT_TIMEOUT_MS);
    // 7.5–15 ms interval for low input lag; 1 s supervision timeout so a
    // controller that dies is detected (and the robot stopped) quickly.
    s_client->setConnectionParams(6, 12, 0, 100);

    // Device info: s_work is this task's copy; publishInfo() hands loop() a snapshot.
    static BtDeviceInfo s_work;
    uint32_t lastLink_ms = 0, lastBattery_ms = 0, rateSeq = 0;

    for (;;) {
        uint32_t now = millis();

        // ── Link dropped: record why (kept so the UI can show the last device) ─
        int drop = _dropReason;
        if (drop) {
            _dropReason = 0;
            if (s_work.connected) {
                s_work.connected   = false;
                s_work.reportCount = _reportSeq - rateSeq + s_work.reportCount;
                s_work.reportRate  = 0;
                strlcpy(s_work.lastDrop, BtInfo::disconnectReason(drop), sizeof(s_work.lastDrop));
                publishInfo(s_work);
            }
        }

        // ── Connected: refresh signal / link / battery / input rate ──────────
        if (_phase == Phase::CONNECTED && s_work.connected) {
            if (now - lastLink_ms >= LINK_INFO_PERIOD_MS) {
                uint32_t seq = _reportSeq;
                s_work.reportRate   = (uint16_t)((seq - rateSeq) * 1000UL / max<uint32_t>(now - lastLink_ms, 1));
                s_work.reportCount += seq - rateSeq;
                rateSeq     = seq;
                lastLink_ms = now;
                BtInfo::readLink(s_client, s_work);
                if (now - lastBattery_ms >= BATTERY_PERIOD_MS) {
                    lastBattery_ms = now;
                    BtInfo::readBattery(s_client, s_work);
                }
                if (s_client->isConnected()) publishInfo(s_work);
            }
        }

        // ── Forget all paired controllers (requested from the web UI) ─────────
        if (_forgetRequest) {
            _forgetRequest = false;
            if (s_client->isConnected()) s_client->disconnect();
            vTaskDelay(pdMS_TO_TICKS(200));
            NimBLEDevice::deleteAllBonds();
            Serial.println("[Pad] All paired controllers forgotten.");
        }

        if (_phase == Phase::SEARCHING) {
            if (s_havePending) {
                s_havePending = false;
                _phase = Phase::CONNECTING;
                if (scan->isScanning()) scan->stop();

                NimBLEAddress addr = s_pendingAddr;
                bool wasBonded = NimBLEDevice::isBonded(addr);
                bool ok = false;
                int subs = 0;
                _dropReason = 0;                // ignore drops from earlier attempts

                if (!s_client->connect(addr)) {
                    Serial.println("[Pad] Connect failed.");
                } else if (!s_client->secureConnection()) {
                    // A stale bond (controller was paired elsewhere since) makes
                    // encryption fail — drop it so the next attempt pairs fresh.
                    Serial.println("[Pad] Pairing/encryption failed.");
                    if (wasBonded) NimBLEDevice::deleteBond(addr);
                } else {
                    // Discover ALL services once, before subscribing. Later lookups
                    // (BtInfo::readStandard) reuse this list; a refresh after
                    // subscribing would silently kill the input notifications.
                    s_client->getServices(true);
                    NimBLERemoteService* hid = s_client->getService(HID_SERVICE_UUID);
                    if (hid) {
                        for (NimBLERemoteCharacteristic* c : hid->getCharacteristics(true)) {
                            if (c->getUUID() == HID_REPORT_UUID && c->canNotify()) {
                                if (c->subscribe(true, notifyCB)) subs++;
                            }
                        }
                    }
                    if (subs > 0) {
                        ok = true;
                    } else {
                        Serial.println("[Pad] Device has no HID input reports — not a gamepad?");
                    }
                }

                if (ok && s_client->isConnected()) {
                    _pairUntil_ms = 0;          // pairing done — close the UI window
                    _phase = Phase::CONNECTED;
                    _connected = true;          // driving works from here on

                    // Then read everything the controller tells about itself
                    // (takes ~0.5 s of GATT reads; input keeps flowing meanwhile).
                    uint16_t connects = s_work.connects + 1;
                    char lastDrop[sizeof(s_work.lastDrop)];
                    strlcpy(lastDrop, s_work.lastDrop, sizeof(lastDrop));
                    s_work = BtDeviceInfo();
                    strlcpy(s_work.kind, "gamepad", sizeof(s_work.kind));
                    strlcpy(s_work.name, _name, sizeof(s_work.name));
                    strlcpy(s_work.lastDrop, lastDrop, sizeof(s_work.lastDrop));
                    s_work.connects       = connects;
                    s_work.hidReports     = (uint8_t)subs;
                    s_work.connected      = true;
                    s_work.connectedAt_ms = millis();
                    rateSeq        = _reportSeq;
                    lastLink_ms    = lastBattery_ms = millis();
                    BtInfo::readStandard(s_client, s_work);
                    if (s_client->isConnected()) {
                        publishInfo(s_work);
                        Serial.printf("[Pad] %s %s | fw %s | VID %04X PID %04X | battery %d%% | RSSI %d dBm\n",
                                      s_work.manufacturer, s_work.model, s_work.fwRev,
                                      s_work.vid, s_work.pid, s_work.battery, s_work.rssi);
                    }
                } else {
                    markFailed(addr);
                    if (s_client->isConnected()) s_client->disconnect();
                    _phase = Phase::SEARCHING;
                }
            } else if (!scan->isScanning()) {
                scan->start(SCAN_PERIOD_MS, false, true);
            }
        } else if (_phase == Phase::CONNECTED) {
            // Normally no scanning while connected. The UI's "Nearby devices"
            // scan is the exception (time-limited by BtNearby).
            bool want = BtNearby::scanWanted();
            if (want && !scan->isScanning())      scan->start(SCAN_PERIOD_MS, false, true);
            else if (!want && scan->isScanning()) scan->stop();
        }

        vTaskDelay(pdMS_TO_TICKS(TASK_PERIOD_MS));
    }
}

#else // ── GAMEPAD_ENABLED not defined: inert stubs ────────────────────────────

void GamepadManager::begin()        { Serial.println("[Pad] Disabled — #define GAMEPAD_ENABLED in config.h to activate."); }
void GamepadManager::update()       {}
void GamepadManager::startPairing() {}
void GamepadManager::forgetAll()    {}
bool GamepadManager::isPairingRequested() const      { return false; }
bool GamepadManager::takeUiUpdate(char*, size_t)     { return false; }
bool GamepadManager::acceptingNewControllers() const { return false; }
const char* GamepadManager::statusStr() const        { return "disabled"; }
String GamepadManager::getName() const               { return String(); }
bool GamepadManager::getDeviceInfo(BtDeviceInfo&)    { return false; }
void GamepadManager::publishInfo(const BtDeviceInfo&) {}
void GamepadManager::applyState(const State&) { (void)applyDeadzone; }
void GamepadManager::stopOutputs() {}
void GamepadManager::_onReport(const uint8_t*, size_t) {}
void GamepadManager::_onDisconnected(int) {}
void GamepadManager::_onAdvertisement(const void*) {}
void GamepadManager::taskEntry(void*) {}
void GamepadManager::taskLoop() {}

#endif // GAMEPAD_ENABLED
