#pragma once
// =============================================================================
// WebServerManager.h — HTTP Server + WebSocket Communication
// =============================================================================
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
using PwmRangeCallback    = std::function<void(uint16_t halfRangeUs)>;
using RenameCallback      = std::function<void(const String& name)>;
using ApPasswordCallback  = std::function<void(const String& pass)>;
using BatteryCalibCallback = std::function<void(float ratio)>;
using EscCalibrateCallback = std::function<void()>;  // Trigger full-range ESC calibration

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
    void onPwmRange(PwmRangeCallback cb)       { _pwmRangeCallback = cb; }
    void onRename(RenameCallback cb)           { _renameCallback = cb; }
    void onApPassword(ApPasswordCallback cb)   { _apPasswordCallback = cb; }
    void onBatteryCalib(BatteryCalibCallback cb) { _batteryCalibCallback = cb; }
    void onEscCalibrate(EscCalibrateCallback cb) { _escCalibrateCallback = cb; }

    // Send a status JSON to all connected browser clients.
    // Call this periodically from loop() — e.g. every 500ms.
    void broadcastStatus(uint8_t batteryPercent, float batteryVoltage, float batteryRatio,
                         bool safetyOk, const String& ipAddress, bool weaponActive,
                         const String& robotName, bool escReady, const char* escCalib,
                         uint8_t weaponSpeedPct);

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
    PwmRangeCallback    _pwmRangeCallback;
    RenameCallback      _renameCallback;
    ApPasswordCallback  _apPasswordCallback;
    BatteryCalibCallback _batteryCalibCallback;
    EscCalibrateCallback _escCalibrateCallback;

    // Handle incoming WebSocket messages
    void handleWebSocketMessage(void* arg, uint8_t* data, size_t len);

    // WebSocket event handler (static to match AsyncWebSocket API)
    void onWebSocketEvent(AsyncWebSocket* server, AsyncWebSocketClient* client,
                          AwsEventType type, void* arg, uint8_t* data, size_t len);
};
