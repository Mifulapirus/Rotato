#pragma once
// =============================================================================
// WiFiManager.h — Access Point + Optional Station Mode
// =============================================================================
// Copyright (c) 2026 Angel Hernandez (www.thehomelab.dev)
// Non-Commercial Source-Available License — see LICENSE for full terms.
// Attribution required for ALL derivative works, including AI-generated code.
//
// Manages WiFi connectivity for the robot controller.
//
// DEFAULT BEHAVIOR (Access Point mode):
//   The robot creates its own WiFi hotspot on boot.
//   SSID: "Rotato-XXXX" (last 4 chars of MAC address, unique per robot)
//   Password: defined in config.h
//   Robot IP: 192.168.4.1
//
// OPTIONAL STATION MODE:
//   If the user enters home WiFi credentials via the web UI, they are saved
//   to flash memory (using Arduino's Preferences / ESP32 NVS storage).
//   On the next boot, the robot will try to connect to that network first.
//   If it can't connect within STA_CONNECT_TIMEOUT_MS, it falls back to AP mode.
//
// WHY NVS (Non-Volatile Storage)?
//   NVS stores small key-value pairs that survive power cycles.
//   Think of it like EEPROM but more reliable and flexible.
// =============================================================================

#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include "config.h"

enum class RobotWiFiMode {
    ACCESS_POINT,   // Robot is its own hotspot
    STATION         // Robot connected to existing WiFi network
};

class WiFiManager {
public:
    // Initialize WiFi. Tries STA if saved credentials exist, otherwise starts AP.
    void begin();

    // Save new WiFi credentials to NVS and reboot into STA mode.
    void saveCredentials(const String& ssid, const String& password);

    // Clear saved credentials AND custom robot name/AP password, return to defaults.
    void clearCredentials();

    // ── Robot identity ───────────────────────────────────────────────────────
    // Save a custom robot name to NVS. Reboots to apply (SSID + BLE advert change).
    // Name must NOT match "Rotato-XXXX" (4 hex chars) — those are reserved defaults.
    // Returns false and does NOT reboot if the name is forbidden or too short.
    bool saveRobotName(const String& name);

    // Reset the robot name to the hardware default ("Rotato-XXXX") and reboot.
    void clearRobotName();

    // Returns the hardware-derived default name ("Rotato-XXXX") — never custom.
    String getDefaultName() const { return _defaultName; }

    // Returns the current active robot name (custom override, or hardware default).
    String getRobotName() const { return _apSSID; }

    // ── AP password ──────────────────────────────────────────────────────────
    // Change the Access Point password stored in NVS. Reboots to apply.
    // Pass must be >= 8 characters (WPA2 minimum).
    bool saveApPassword(const String& pass);

    // Returns the currently active AP password (custom or compiled-in default).
    String getApPassword() const { return _apPassword; }

    // Returns the current WiFi mode (ACCESS_POINT or STATION)
    RobotWiFiMode getMode() const { return _mode; }

    // Returns the robot's current IP address as a string
    String getIPAddress() const;

    // Returns the full AP SSID (e.g. "Rotato-A3F2")
    String getSSID() const { return _apSSID; }

private:
    RobotWiFiMode _mode = RobotWiFiMode::ACCESS_POINT;
    String      _apSSID;       // active SSID (custom name or default)
    String      _defaultName;  // hardware-derived "Rotato-XXXX", never changes
    String      _apPassword;   // active AP password (custom or AP_PASSWORD)
    Preferences _prefs;        // NVS storage handle

    // Try to connect to saved WiFi network. Returns true if successful.
    bool connectToSavedNetwork();

    // Start the robot's own Access Point hotspot
    void startAccessPoint();

    // Generate unique SSID using last 4 chars of MAC address
    String buildSSID();
};
