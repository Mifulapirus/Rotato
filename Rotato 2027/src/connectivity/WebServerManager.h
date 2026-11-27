#pragma once
// =============================================================================
// WebServerManager.h — HTTP Server + WebSocket Communication
// =============================================================================
// Copyright (c) 2026 Angel Hernandez (www.thehomelab.dev)
// Non-Commercial Source-Available License — see LICENSE for full terms.
// Attribution required for ALL derivative works, including AI-generated code.
//
// This class runs the robot's web server and handles real-time communication
// with the browser using WebSockets.
//
// HOW IT WORKS:
//   1. The web server serves "index.html" from the LittleFS filesystem.
//      (Upload it with "pio run --target uploadfs")
//   2. The browser connects via WebSocket at ws://<robot-ip>/ws
//   3. The browser sends JSON commands to control the robot.
//   4. The robot sends JSON status updates back to the browser every 500ms.
//
// JSON PROTOCOL (Browser → Robot):
//   Drive:   {"type":"drive","x":0.5,"y":0.8}
//   Weapon:  {"type":"weapon","active":true}
//   WiFi:    {"type":"settings","ssid":"MyNetwork","pass":"password123"}
//   Forget:  {"type":"forget"}
//   Save:    {"type":"network_save","ssid":"","pass":"","name":"","resetName":false,"apPass":""}
//            — any of WiFi / name / hotspot password, one reboot ("" = unchanged)
//   Gamepad: {"type":"gamepad_pair"}    — open the controller pairing window
//            {"type":"gamepad_forget"}  — forget all paired controllers
//   Invert:  {"type":"drive_reverse","reversed":true} — invert motors direction (all inputs)
//
// JSON PROTOCOL (Robot → Browser, every 500ms):
//   {"battery":85,"safety":true,"ip":"192.168.4.1","weapon":false,"fw":"1.0.0+42"}
//
// CALLBACKS:
//   Instead of directly calling motors from here, we use callbacks.
//   This keeps the web layer separate from the hardware layer — a good
//   software design practice (called "separation of concerns").
// =============================================================================

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "config.h"

// ── Callback function types ────────────────────────────────────────────────
// These are function pointer types used to notify main.cpp when a command arrives.
using DriveCallback       = std::function<void(float x, float y)>;
using WeaponCallback      = std::function<void(bool active)>;
using WeaponSpeedCallback = std::function<void(float speed)>;          // slider 0.0–1.0
using WeaponAccelCallback = std::function<void(uint16_t rampMs, uint8_t curve)>;
using SettingsCallback    = std::function<void(const String& ssid, const String& pass)>;
using ForgetCallback      = std::function<void()>;
using BleEnableCallback   = std::function<void(bool enabled)>;
using RenameCallback      = std::function<void(const String& name)>;
using ApPasswordCallback  = std::function<void(const String& pass)>;
using NetworkSaveCallback = std::function<void(const String& ssid, const String& pass,
                                               const String& name, bool resetName, const String& apPass)>;
using BatteryCalibCallback  = std::function<void(float ratio)>;
using BatteryCellsCallback  = std::function<void(uint8_t cells)>;
using EscCalibrateCallback  = std::function<void()>;  // Trigger full-range ESC calibration
using GamepadCommandCallback = std::function<void()>; // Gamepad pair / forget
using DriveReverseCallback   = std::function<void(bool reversed)>;

class WebServerManager {
public:
    WebServerManager();

    // Start the web server and WebSocket endpoint.
    // Call after WiFi and LittleFS are initialized.
    void begin();

    // Register callback functions for robot commands.
    // Call these before begin() to wire up the hardware.
    void onDrive(DriveCallback cb)             { _driveCallback = cb; }
    void onWeapon(WeaponCallback cb)           { _weaponCallback = cb; }
    void onWeaponSpeed(WeaponSpeedCallback cb) { _weaponSpeedCallback = cb; }
    void onWeaponAccel(WeaponAccelCallback cb) { _weaponAccelCallback = cb; }
    void onSettings(SettingsCallback cb)       { _settingsCallback = cb; }
    void onForget(ForgetCallback cb)           { _forgetCallback = cb; }
    void onBleEnable(BleEnableCallback cb)     { _bleEnableCallback = cb; }
    void onRename(RenameCallback cb)           { _renameCallback = cb; }
    void onApPassword(ApPasswordCallback cb)   { _apPasswordCallback = cb; }
    void onNetworkSave(NetworkSaveCallback cb) { _networkSaveCallback = cb; }
    void onBatteryCalib(BatteryCalibCallback cb)    { _batteryCalibCallback = cb; }
    void onBatteryCells(BatteryCellsCallback cb)    { _batteryCellsCallback = cb; }
    void onEscCalibrate(EscCalibrateCallback cb)    { _escCalibrateCallback = cb; }
    void onGamepadPair(GamepadCommandCallback cb)   { _gamepadPairCallback = cb; }
    void onGamepadForget(GamepadCommandCallback cb) { _gamepadForgetCallback = cb; }
    void onDriveReverse(DriveReverseCallback cb)    { _driveReverseCallback = cb; }
    void onRobotsScan(GamepadCommandCallback cb)    { _robotsScanCallback = cb; }
    void onBtScan(GamepadCommandCallback cb)        { _btScanCallback = cb; }

    // mDNS address shown in the UI ("rotato-7c1a.local"); taken = preferred name was in use.
    void setHostname(const String& host, bool taken) { _hostname = host; _hostTaken = taken; }

    // Game controller state, included in the next status broadcast.
    //   state: "disabled" | "searching" | "pairing" | "connecting" | "connected"
    void setGamepadStatus(const char* state, const String& name) { _gamepadState = state; _gamepadName = name; }

    // Saved "Invert Motors Direction" setting, reported in status so every browser agrees.
    void setDriveReversed(bool on) { _driveReversed = on; }
    // Saved weapon acceleration, reported in status for the same reason.
    void setWeaponAccel(uint16_t rampMs, uint8_t curve) { _weaponRampMs = rampMs; _weaponCurve = curve; }

    // Send a pre-built JSON message to every connected browser (no-op if none).
    void broadcastText(const char* json) { if (_ws.count() > 0) _ws.textAll(json); }

    // Number of browsers currently connected over WebSocket.
    size_t clientCount() { return _ws.count(); }

    // Send a status JSON to all connected browser clients.
    // Call this periodically from loop() — e.g. every 500ms.
    void broadcastStatus(uint8_t batteryPercent, float batteryVoltage, float batteryRatio,
                         uint8_t batteryCells, bool safetyOk, const String& ipAddress,
                         bool weaponActive, const String& robotName, bool escReady,
                         const char* escCalib, uint8_t weaponSpeedPct);

    // WebSocket cleanup — must be called from loop()
    void update();

private:
    AsyncWebServer _server;
    AsyncWebSocket _ws;

    DriveCallback       _driveCallback;
    WeaponCallback      _weaponCallback;
    WeaponSpeedCallback _weaponSpeedCallback;
    WeaponAccelCallback _weaponAccelCallback;
    SettingsCallback    _settingsCallback;
    ForgetCallback      _forgetCallback;
    BleEnableCallback   _bleEnableCallback;
    RenameCallback      _renameCallback;
    ApPasswordCallback  _apPasswordCallback;
    NetworkSaveCallback _networkSaveCallback;
    BatteryCalibCallback  _batteryCalibCallback;
    BatteryCellsCallback  _batteryCellsCallback;
    EscCalibrateCallback  _escCalibrateCallback;
    GamepadCommandCallback _gamepadPairCallback;
    GamepadCommandCallback _gamepadForgetCallback;
    DriveReverseCallback   _driveReverseCallback;
    bool                   _driveReversed = false;
    uint16_t               _weaponRampMs  = WEAPON_RAMP_MS_DEFAULT;
    uint8_t                _weaponCurve   = WEAPON_CURVE_DEFAULT;
    GamepadCommandCallback _robotsScanCallback;
    GamepadCommandCallback _btScanCallback;
    String                _hostname;
    bool                  _hostTaken = false;
    const char*           _gamepadState = "disabled";
    String                _gamepadName;

    // Handle incoming WebSocket messages
    void handleWebSocketMessage(void* arg, uint8_t* data, size_t len);

    // WebSocket event handler (static to match AsyncWebSocket API)
    void onWebSocketEvent(AsyncWebSocket* server, AsyncWebSocketClient* client,
                          AwsEventType type, void* arg, uint8_t* data, size_t len);
};
