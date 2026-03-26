// =============================================================================
// main.cpp — Rotato Robot Controller
// Asmbly Workshop — Democratic Robot 2026
// =============================================================================
//
// This is the top-level file that ties everything together.
//
// ARCHITECTURE OVERVIEW:
//
//   ┌─────────────────────────────────────────────────────────────┐
//   │                        main.cpp                             │
//   │   Wires hardware drivers ↔ connectivity layer               │
//   └──────────┬───────────────────────────┬───────────────────── ┘
//              │                           │
//   ┌──────────▼─────────┐    ┌────────────▼───────────────────── ┐
//   │   hardware/         │    │   connectivity/                   │
//   │   MotorController   │    │   WiFiManager                     │
//   │   BuzzerController  │◄───│   WebServerManager  (WebSocket)   │
//   │   BatteryMonitor    │    │   BLEManager        (stub)        │
//   │   SafetySwitch      │    └───────────────────────────────────┘
//   └─────────────────────┘
//
// STARTUP SEQUENCE:
//   1. Serial (for debugging)
//   2. Hardware drivers (motors to neutral, battery monitor, buzzer, safety)
//   3. WiFi (AP or STA mode)
//   4. Web server + WebSocket
//   5. BLE stubs (disabled by default — see config.h)
//   6. Startup beep and ready indication
//
// THE LOOP:
//   - Drive commands arrive via WebSocket callbacks (event-driven, not polled)
//   - Safety switch is checked every loop to override weapon if needed
//   - Battery and status are broadcast to browser every WS_STATUS_INTERVAL_MS
//   - Buzzer sequences are stepped forward (non-blocking)
// =============================================================================

#include <Arduino.h>
#include "config.h"
#include "version_build.h"

// ── Hardware drivers ─────────────────────────────────────────────────────────
#include "hardware/MotorController.h"
#include "hardware/BuzzerController.h"
#include "hardware/BatteryMonitor.h"
#include "hardware/SafetySwitch.h"

// ── Connectivity ─────────────────────────────────────────────────────────────
#include "connectivity/WiFiManager.h"
#include "connectivity/WebServerManager.h"
#include "connectivity/BLEManager.h"

// ── Module instances ──────────────────────────────────────────────────────────
MotorController  motors;
BuzzerController buzzer;
BatteryMonitor   battery;
SafetySwitch     safety;
WiFiManager      wifi;
WebServerManager webServer;
BLEManager       ble;

// ── Robot state ──────────────────────────────────────────────────────────────
enum class RobotState {
    STARTUP,
    IDLE,
    READY,
    WEAPON_ACTIVE
};

RobotState robotState = RobotState::STARTUP;
uint32_t   lastStatusBroadcast_ms = 0;
uint32_t   lastBatteryRead_ms = 0;

// ── Setup ─────────────────────────────────────────────────────────────────────
void setup() {
    // ── 1. Serial ────────────────────────────────────────────────────────────
    Serial.begin(115200);

    // ESP32-C3 Super Mini uses USB-CDC — wait up to 5s for monitor to connect.
    {
        uint32_t t0 = millis();
        while (!Serial && (millis() - t0 < 5000)) { delay(10); }
    }

    Serial.println("\n========================================");
    Serial.println("  Rotato Robot Controller");
    Serial.println("  Weaponized Plastic — 2026");
    Serial.println("========================================\n");
    Serial.println("[Boot] Serial OK");
    Serial.printf("[Boot] Firmware v%s  (built %s %s)\n", FW_VERSION_FULL, FW_BUILD_DATE, FW_BUILD_TIME);

    // ── 2. Hardware drivers ──────────────────────────────────────────────────
    motors.begin();
    battery.begin();
    buzzer.begin();
    safety.begin();

    // ── 3. WiFi ──────────────────────────────────────────────────────────────
    // Uncomment to wipe saved credentials if the AP doesn't appear:
    // wifi.clearCredentials();
    wifi.begin();

    // ── 4. Web server ────────────────────────────────────────────────────────
    webServer.onDrive([](float x, float y) {
        motors.setDrive(x, y);
    });

    webServer.onWeapon([](bool active) {
        if (active && !safety.isSafe()) {
            Serial.println("[Main] Weapon command blocked by safety switch!");
            return;
        }
        motors.setWeapon(active);
        if (active) {
            buzzer.beepWeaponOn();
            robotState = RobotState::WEAPON_ACTIVE;
        } else {
            buzzer.beepWeaponOff();
            robotState = RobotState::READY;
        }
    });

    webServer.onSettings([](const String& ssid, const String& pass) {
        wifi.saveCredentials(ssid, pass);
    });

    webServer.begin();

    // ── 5. BLE stubs ─────────────────────────────────────────────────────────
    ble.begin();

    // ── 6. Ready ─────────────────────────────────────────────────────────────
    robotState = RobotState::IDLE;
    buzzer.beepStartup();

    Serial.println("\n[Main] Robot ready!");
    Serial.printf("[Main] Connect to WiFi SSID: %s\n", wifi.getSSID().c_str());
    Serial.printf("[Main] Then open browser at:  http://%s\n", wifi.getIPAddress().c_str());
    Serial.println("[Main] ----------------------------------------");
}

// ── Loop ──────────────────────────────────────────────────────────────────────
void loop() {
    uint32_t now = millis();

    // Safety switch — stop weapon if triggered
    safety.update();
    if (motors.isWeaponActive() && !safety.isSafe()) {
        Serial.println("[Main] Safety switch triggered! Stopping weapon.");
        motors.stopAll();
        buzzer.beepError();
        robotState = RobotState::READY;
    }

    // Battery ADC sample
    if (now - lastBatteryRead_ms >= WS_STATUS_INTERVAL_MS) {
        battery.update();
        lastBatteryRead_ms = now;
    }

    // Status broadcast to browser
    if (now - lastStatusBroadcast_ms >= WS_STATUS_INTERVAL_MS) {
        webServer.broadcastStatus(
            battery.getPercent(),
            safety.isSafe(),
            wifi.getIPAddress(),
            motors.isWeaponActive()
        );
        ble.sendStatus(battery.getPercent(), safety.isSafe(), motors.isWeaponActive());
        lastStatusBroadcast_ms = now;
    }

    buzzer.update();
    webServer.update();
    ble.update();
}
