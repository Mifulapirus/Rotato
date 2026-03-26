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
using DriveCallback   = std::function<void(float x, float y)>;
using WeaponCallback  = std::function<void(bool active)>;
using SettingsCallback = std::function<void(const String& ssid, const String& pass)>;

class WebServerManager {
public:
    WebServerManager();

    // Start the web server and WebSocket endpoint.
    // Call after WiFi and LittleFS are initialized.
    void begin();

    // Register callback functions for robot commands.
    // Call these before begin() to wire up the hardware.
    void onDrive(DriveCallback cb)      { _driveCallback = cb; }
    void onWeapon(WeaponCallback cb)    { _weaponCallback = cb; }
    void onSettings(SettingsCallback cb) { _settingsCallback = cb; }

    // Send a status JSON to all connected browser clients.
    // Call this periodically from loop() — e.g. every 500ms.
    void broadcastStatus(uint8_t batteryPercent, bool safetyOk,
                         const String& ipAddress, bool weaponActive);

    // WebSocket cleanup — must be called from loop()
    void update();

private:
    AsyncWebServer _server;
    AsyncWebSocket _ws;

    DriveCallback    _driveCallback;
    WeaponCallback   _weaponCallback;
    SettingsCallback _settingsCallback;

    // Handle incoming WebSocket messages
    void handleWebSocketMessage(void* arg, uint8_t* data, size_t len);

    // WebSocket event handler (static to match AsyncWebSocket API)
    void onWebSocketEvent(AsyncWebSocket* server, AsyncWebSocketClient* client,
                          AwsEventType type, void* arg, uint8_t* data, size_t len);
};
