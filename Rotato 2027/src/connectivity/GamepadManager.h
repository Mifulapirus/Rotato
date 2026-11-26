#pragma once
// =============================================================================
// GamepadManager.h — Bluetooth LE game controller (Xbox Wireless Controller)
// =============================================================================
// Copyright (c) 2026 Angel Hernandez (www.thehomelab.dev)
// Non-Commercial Source-Available License — see LICENSE for full terms.
// Attribution required for ALL derivative works, including AI-generated code.
//
// Connects to an Xbox Wireless Controller as a BLE HID host (NimBLE client
// role). It runs alongside BLEManager's GATT server on the same NimBLE stack.
//
// THREADS:
//   - A small FreeRTOS task ("gamepad") does the slow, blocking work: scanning,
//     connecting, pairing and subscribing. Connecting can take seconds, so it
//     must never run inside loop().
//   - Controller input reports arrive on the NimBLE host task and are copied
//     into a buffer under a spinlock.
//   - update() runs in loop(): it reads the latest report, applies the button
//     mapping (config.h → "Game Controller") and fires the drive / weapon
//     callbacks. So callbacks always run on the Arduino loop task, exactly like
//     the rest of main.cpp expects.
//
// Commands are sent only when the controller's output CHANGES, so the gamepad
// and the web UI can both be used — the most recent input wins.
//
// XBOX BLE INPUT REPORT (16 bytes, little-endian):
//   [0-1] LX  [2-3] LY  [4-5] RX  [6-7] RY   uint16, 0…65535, center 32768, Y: 0 = up
//   [8-9] LT  [10-11] RT                     uint16, 0…1023
//   [12]  D-pad hat: 0 = center, 1 = up, 2 = up-right … 8 = up-left (clockwise)
//   [13]  A=0x01 B=0x02 X=0x08 Y=0x10 LB=0x40 RB=0x80
//   [14]  View=0x04 Menu=0x08 Xbox=0x10 LS=0x20 RS=0x40
//   [15]  Share=0x01
//
// USAGE (main.cpp):
//   gamepad.onDrive([](float x, float y){ ... });
//   gamepad.onWeaponSpeed([](float s){ ... });
//   gamepad.onConnection([](bool connected){ ... });
//   gamepad.begin();   // after ble.begin() so NimBLE is already initialized
//   // loop():
//   gamepad.update();
// =============================================================================

#include <Arduino.h>
#include <functional>
#include "config.h"
#include "BtDeviceInfo.h"

class GamepadManager {
public:
    // Normalized controller state (after parsing, before mapping).
    struct State {
        float    lx = 0, ly = 0, rx = 0, ry = 0;   // -1…+1, Y positive = up
        float    lt = 0, rt = 0;                   //  0…1
        uint8_t  dpad = 0;                         // hat value 0–8
        uint32_t buttons = 0;                      // BTN_* bitmask
        uint8_t  raw[20] = {};                     // the report as received (for the web UI)
        uint8_t  rawLen  = 0;
    };

    static constexpr uint32_t BTN_A = 1u << 0, BTN_B = 1u << 1, BTN_X = 1u << 2, BTN_Y = 1u << 3,
                              BTN_LB = 1u << 4, BTN_RB = 1u << 5, BTN_VIEW = 1u << 6,
                              BTN_MENU = 1u << 7, BTN_XBOX = 1u << 8, BTN_LS = 1u << 9,
                              BTN_RS = 1u << 10, BTN_SHARE = 1u << 11;

    using DriveCallback       = std::function<void(float x, float y)>;
    using WeaponSpeedCallback = std::function<void(float speed)>;
    using ConnectionCallback  = std::function<void(bool connected)>;
    using ButtonCallback      = std::function<void(uint32_t pressed)>;  // BTN_* bits just pressed

    void onDrive(DriveCallback cb)             { _driveCallback = cb; }
    void onWeaponSpeed(WeaponSpeedCallback cb) { _weaponCallback = cb; }
    void onConnection(ConnectionCallback cb)   { _connCallback = cb; }
    // Fires once per press (on the press, not while held) with the BTN_* bits
    // that just went down. Runs on the loop task.
    void onButtonPress(ButtonCallback cb)      { _buttonCallback = cb; }

    // Start the background task. Initializes NimBLE if BLEManager hasn't.
    void begin();

    // Call every loop(): applies the latest controller input and fires callbacks.
    void update();

    // Open the pairing window so a NEW controller can be paired. Safe to call
    // from any task (e.g. a WebSocket callback).
    void startPairing();

    // Disconnect and forget every paired controller. Safe from any task.
    void forgetAll();

    bool        isConnected() const { return _connected; }
    bool        isPairingRequested() const;   // the UI-triggered window is open
    const char* statusStr() const;            // "disabled", "searching", "pairing", "connecting", "connected"
    String      getName() const;              // name of the connected controller ("" if none)

    // Everything read from the controller (Device Information, battery, link
    // quality, stats…). Returns false if no controller has connected since boot.
    // After a disconnect it keeps describing the last controller (connected =
    // false, lastDrop = why). Safe from loop().
    bool getDeviceInfo(BtDeviceInfo& out);

    // Live controller state for the web UI. Returns true (and fills buf with a
    // JSON message) only when something changed since the last call.
    //   {"type":"gamepad","lx":0.00,"ly":0.00,"rx":0.00,"ry":0.00,"lt":0.00,"rt":0.00,
    //    "dpad":0,"btn":0,"x":0.00,"y":0.00,"w":0.00,"lock":1,"split":0,"raw":"0080…"}
    //   lx…rt = raw inputs, btn = BTN_* mask, x/y/w = drive + weapon actually sent
    //   to the motors, lock = weapon locked until RT is released, raw = the HID
    //   report bytes in hex (handy when mapping a different controller).
    // Call from loop() only.
    bool takeUiUpdate(char* buf, size_t len);

    // ── Internal — called from NimBLE callbacks, not for external use ────────
    void _onReport(const uint8_t* data, size_t len);
    void _onDisconnected(int reason);
    void _onAdvertisement(const void* advertisedDevice);

private:
    enum class Phase : uint8_t { INACTIVE, SEARCHING, CONNECTING, CONNECTED };

    DriveCallback       _driveCallback;
    WeaponSpeedCallback _weaponCallback;
    ConnectionCallback  _connCallback;
    ButtonCallback      _buttonCallback;

    // Shared between tasks
    volatile Phase    _phase          = Phase::INACTIVE;
    volatile bool     _connected      = false;
    volatile bool     _forgetRequest  = false;
    volatile uint32_t _pairUntil_ms   = 0;     // UI pairing window end (0 = closed)
    volatile uint32_t _reportSeq      = 0;     // bumped on every input report
    State             _shared;                 // latest report (guarded by _mux)
    portMUX_TYPE      _mux            = portMUX_INITIALIZER_UNLOCKED;
    char              _name[32]       = "";
    BtDeviceInfo      _info;                   // written by the gamepad task only (guarded by _infoMux)
    bool              _haveInfo       = false;
    volatile int      _dropReason     = 0;     // set by the disconnect callback for the gamepad task
    portMUX_TYPE      _infoMux        = portMUX_INITIALIZER_UNLOCKED;

    // Loop-task only
    bool     _wasConnected  = false;
    uint32_t _seenSeq       = 0;
    float    _lastX = 0, _lastY = 0;
    float    _lastWeapon    = 0;
    bool     _weaponLockout = true;   // RT must be released once before it works
    State    _lastState;              // last applied input (for the web UI)
    uint32_t _prevButtons   = 0;      // for press (edge) detection
    bool     _uiDirty       = false;  // _lastState / outputs changed since last takeUiUpdate()

    bool acceptingNewControllers() const;
    void applyState(const State& s);
    void stopOutputs();

    static void taskEntry(void* arg);
    void taskLoop();
    void publishInfo(const BtDeviceInfo& info);
};
