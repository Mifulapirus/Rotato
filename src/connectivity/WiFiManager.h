#pragma once
// =============================================================================
// WiFiManager.h — Access Point + Optional Station Mode
// =============================================================================
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

    // Clear saved credentials and restart in AP mode.
    void clearCredentials();

    // Returns the current WiFi mode (ACCESS_POINT or STATION)
    RobotWiFiMode getMode() const { return _mode; }

    // Returns the robot's current IP address as a string
    String getIPAddress() const;

    // Returns the full AP SSID (e.g. "Rotato-A3F2")
    String getSSID() const { return _apSSID; }

private:
    RobotWiFiMode _mode = RobotWiFiMode::ACCESS_POINT;
    String      _apSSID;
    Preferences _prefs;  // NVS storage handle

    // Try to connect to saved WiFi network. Returns true if successful.
    bool connectToSavedNetwork();

    // Start the robot's own Access Point hotspot
    void startAccessPoint();

    // Generate unique SSID using last 4 chars of MAC address
    String buildSSID();
};
