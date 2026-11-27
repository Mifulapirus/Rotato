// =============================================================================
// main.cpp — Rotato Robot Controller
// Rotato 2027
// =============================================================================
// Copyright (c) 2026 Angel Hernandez (www.thehomelab.dev)
// Non-Commercial Source-Available License — see LICENSE file for full terms.
// Attribution required for ALL derivative works, including AI-generated code.
// Commercial use prohibited without explicit written permission.
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
//   │   SafetySwitch      │    │   GamepadManager    (Xbox BLE)    │
//   │   StatusLed         │    └───────────────────────────────────┘
//   └─────────────────────┘
//
// STARTUP SEQUENCE:
//   1. Serial (for debugging)
//   2. Hardware drivers (motors to neutral, battery monitor, buzzer, safety)
//   3. WiFi (AP or STA mode)
//   4. Web server + WebSocket
//   5. BLE GATT server (Android app) + Bluetooth game controller
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
#include "hardware/StatusLed.h"

// ── Connectivity ─────────────────────────────────────────────────────────────
#include "connectivity/WiFiManager.h"
#include "connectivity/WebServerManager.h"
#include "connectivity/BLEManager.h"
#include "connectivity/GamepadManager.h"
#include "connectivity/NetDiscovery.h"
#include "connectivity/BtNearby.h"

// ── Module instances ──────────────────────────────────────────────────────────
MotorController  motors;
BuzzerController buzzer;
BatteryMonitor   battery;
SafetySwitch     safety;
WiFiManager      wifi;
WebServerManager webServer;
BLEManager       ble;
GamepadManager   gamepad;
NetDiscovery     discovery;
StatusLed        statusLed;

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
uint32_t   lastGamepadUi_ms = 0;
uint32_t   lastBtInfo_ms = 0;

// ── Bluetooth tab: details of every Bluetooth device ────────────────────────
// Sends {"type":"bt", "addr", "maxLinks", "bonds":[…], "phone", "devices":[…]}.
// Each device is a BtDeviceInfo (see BtDeviceInfo.h); the browser renders any
// number of them, so a future Bluetooth device just adds one line below.
static void broadcastBluetoothInfo() {
    if (webServer.clientCount() == 0) return;
    JsonDocument doc;
    doc["type"]  = "bt";
    doc["phone"] = ble.isConnected();          // Rotato phone app (robot is the peripheral)
    BtInfo::addLocal(doc);
    JsonArray devices = doc["devices"].to<JsonArray>();
    BtDeviceInfo info;
    if (gamepad.getDeviceInfo(info)) BtInfo::toJson(info, devices.add<JsonObject>());
    // e.g. if (remote.getDeviceInfo(info)) BtInfo::toJson(info, devices.add<JsonObject>());
    String out;
    serializeJson(doc, out);
    webServer.broadcastText(out.c_str());
}

// ── Boot button state (GPIO0, hold 3s to clear WiFi credentials) ─────────────
static uint32_t bootBtnPressTime_ms = 0;
static bool     bootBtnHandled      = false;

// ── Status LED — pick the most important thing to show (see config.h) ──────
static void updateStatusLed() {
    using P = StatusLed::Pattern;
    bool anyoneConnected = webServer.clientCount() > 0 || ble.isConnected() || gamepad.isConnected();

    P p;
    if (motors.isWeaponActive())                                p = P::WEAPON;
    else if (!motors.isEscReady())                              p = P::ESC_ARMING;
    else if (battery.getPercent() <= STATUS_LED_LOW_BATT_PCT)   p = P::BATTERY_LOW;
    else if (gamepad.isPairingRequested())                      p = P::PAIRING;
    else if (anyoneConnected)                                   p = P::SOLID;
    else                                                        p = P::HEARTBEAT;

    statusLed.setPattern(p);
    statusLed.update();
}

// ── Setup ─────────────────────────────────────────────────────────────────────
void setup() {
    // ── 1. Serial ────────────────────────────────────────────────────────────
    Serial.begin(115200);

    // On USB-CDC boards this waits up to 5s for a monitor. The D1 Mini uses a
    // USB-UART chip, where Serial is always ready, so this returns immediately.
    {
        uint32_t t0 = millis();
        while (!Serial && (millis() - t0 < 5000)) { delay(10); }
    }
    for(int i=0; i<10; i++) {
        Serial.print(".");
        delay(100);
    }
    Serial.println();
    Serial.println("\n========================================");
    Serial.println("  Rotato Robot Controller");
    Serial.println("  Weaponized Plastic - Rotato 2027");
    Serial.println("========================================\n");
    Serial.println("[Boot] Serial OK");
    Serial.printf("[Boot] Firmware %s for PCB rev %s  (built %s %s)\n", FW_VERSION_FULL, PCB_REVISION_STR, FW_BUILD_DATE, FW_BUILD_TIME);

    // ── Chip info ────────────────────────────────────────────────────────────
    {
        static const char* const FM_NAMES[] = { "QIO","QOUT","DIO","DOUT","Fast","Slow" };
        uint8_t  fm  = (uint8_t)ESP.getFlashChipMode();
        uint64_t mac = ESP.getEfuseMac();
        Serial.println("[Boot] ----------------------------------------");
        Serial.printf( "[Boot]  Chip        : %s  rev %u  (%u core)\n",
                        ESP.getChipModel(), ESP.getChipRevision(), ESP.getChipCores());
        Serial.printf( "[Boot]  CPU Freq    : %u MHz\n",      ESP.getCpuFreqMHz());
        Serial.printf( "[Boot]  Flash       : %u MB  @ %u MHz  (%s)\n",
                        ESP.getFlashChipSize() / (1024*1024),
                        ESP.getFlashChipSpeed() / 1000000,
                        fm < 6 ? FM_NAMES[fm] : "unknown");
        Serial.printf( "[Boot]  Heap        : %u B total / %u B free\n",
                        ESP.getHeapSize(), ESP.getFreeHeap());
        Serial.printf( "[Boot]  SDK         : %s\n",           ESP.getSdkVersion());
        Serial.printf( "[Boot]  MAC (base)  : %02X:%02X:%02X:%02X:%02X:%02X\n",
                        (uint8_t)(mac),       (uint8_t)(mac >> 8),  (uint8_t)(mac >> 16),
                        (uint8_t)(mac >> 24), (uint8_t)(mac >> 32), (uint8_t)(mac >> 40));
        Serial.println("[Boot] ----------------------------------------");
    }

    // ── 2. Hardware drivers ──────────────────────────────────────────────────
    motors.begin();
    battery.begin();
    buzzer.begin();
    safety.begin();
    statusLed.begin();
    statusLed.setPattern(StatusLed::Pattern::ESC_ARMING);

    // Boot button — active LOW, internal pull-up
    pinMode(PIN_BOOT_BUTTON, INPUT_PULLUP);

    // ── 3. WiFi ──────────────────────────────────────────────────────────────
    // Uncomment to wipe saved credentials if the AP doesn't appear:
    // wifi.clearCredentials();
    wifi.begin();
    // http://<robot-name>.local — see NetDiscovery.h for how several robots coexist
    discovery.begin(NetDiscovery::toHostname(wifi.getRobotName()),
                    NetDiscovery::toHostname(wifi.getDefaultName()),
                    wifi.getRobotName(), wifi.getDefaultName());

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

    webServer.onForget([]() {
        wifi.clearCredentials();
    });

    webServer.onBleEnable([](bool en) {
        ble.setEnabled(en);
    });

    webServer.onRename([](const String& name) {
        // Empty name = reset to hardware default.
        // WiFiManager validates the reserved-name pattern too.
        if (name.isEmpty()) {
            wifi.clearRobotName();  // saves + reboots
        } else {
            wifi.saveRobotName(name);  // saves + reboots
        }
    });

    webServer.onNetworkSave([](const String& ssid, const String& pass,
                               const String& name, bool resetName, const String& apPass) {
        wifi.saveSettings(ssid, pass, name, resetName, apPass);   // reboots if saved
    });

    webServer.onApPassword([](const String& pass) {
        wifi.saveApPassword(pass);  // saves + reboots
    });

    webServer.onBatteryCalib([](float ratio) {
        battery.setRatio(ratio);
    });

    webServer.onBatteryCells([](uint8_t cells) {
        battery.setBatteryCells(cells);
    });

    webServer.onEscCalibrate([]() {
        if (!motors.isEscReady()) {
            Serial.println("[Main] ESC calibration rejected — ESC still arming.");
            return;
        }
        motors.startEscFullCalib();
        buzzer.beepWeaponOn();  // audible cue that calibration started
    });

    webServer.onWeaponSpeed([](float speed) {
        if (speed > 0.0f && !safety.isSafe()) {
            Serial.println("[Main] Weapon speed blocked by safety switch!");
            return;
        }
        motors.setWeaponSpeed(speed);
        if (speed > 0.0f) {
            robotState = RobotState::WEAPON_ACTIVE;
        } else {
            buzzer.beepWeaponOff();
            robotState = RobotState::READY;
        }
    });

    webServer.onWeaponAccel([](uint16_t rampMs, uint8_t curve) {
        motors.setWeaponRamp(rampMs);
        motors.setWeaponCurve(curve);
    });

    webServer.begin();

    // ── 5. BLE ───────────────────────────────────────────────────────────────
    // Device name matches the robot name (custom or default "Rotato-XXXX").
    ble.begin(wifi.getRobotName());
    ble.onDrive([](float x, float y) {
        motors.setDrive(x, y);
    });
    ble.onWeapon([](bool active) {
        if (active && !safety.isSafe()) {
            Serial.println("[Main] BLE weapon command blocked by safety switch!");
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

    // ── 5b. Bluetooth game controller (Xbox) ─────────────────────────────────
    // Must start after ble.begin() — both share the NimBLE stack.
    gamepad.onDrive([](float x, float y) {
        motors.setDrive(x, y);
    });
    gamepad.onWeaponSpeed([](float speed) {
        if (speed > 0.0f && !safety.isSafe()) {
            Serial.println("[Main] Gamepad weapon blocked by safety switch!");
            return;
        }
        bool wasActive = motors.isWeaponActive();
        motors.setWeaponSpeed(speed);
        if (motors.isWeaponActive()) {
            if (!wasActive) buzzer.beepWeaponOn();
            robotState = RobotState::WEAPON_ACTIVE;
        } else {
            if (wasActive) buzzer.beepWeaponOff();
            robotState = RobotState::READY;
        }
    });
    gamepad.onConnection([](bool connected) {
        if (connected) buzzer.beepArmed();
        else           buzzer.beepError();
    });
    gamepad.onButtonPress([](uint32_t pressed) {
        if (pressed & GamepadManager::BTN_A)  buzzer.beepNope();         // A = little error sound
        if (pressed & GamepadManager::BTN_LB) buzzer.beepPrecisionOn();  // precision mode on
    });
    gamepad.onButtonRelease([](uint32_t released) {
        if (released & GamepadManager::BTN_LB) buzzer.beepPrecisionOff();
    });
    webServer.onRobotsScan([]()    { discovery.requestScan(); });
    webServer.onBtScan([]()        { BtNearby::requestScan(BT_NEARBY_SCAN_MS); });
    webServer.setHostname(String(discovery.hostname()) + ".local", discovery.preferredTaken());
    webServer.onGamepadPair([]()   { gamepad.startPairing(); });
    webServer.onGamepadForget([]() { gamepad.forgetAll(); });
    webServer.onDriveReverse([](bool on) { motors.setDriveReversed(on); });
    webServer.setDriveReversed(motors.isDriveReversed());
    webServer.setWeaponAccel(motors.getWeaponRampMs(), motors.getWeaponCurve());
    gamepad.begin();

    // ── 6. Ready ─────────────────────────────────────────────────────────────
    robotState = RobotState::IDLE;
    buzzer.beepStartup();

    Serial.println("\n[Main] Robot ready!");
    Serial.printf("[Main] Connect to WiFi SSID: %s\n", wifi.getRobotName().c_str());
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
    // Safety switch during ESC calibration — abort calibration
    if (motors.isEscCalibrating() && !safety.isSafe()) {
        Serial.println("[Main] Safety switch triggered during ESC calibration — aborting.");
        motors.stopAll();
        buzzer.beepError();
    }

    // Advance ESC arming timer and calibration state machine
    motors.updateEscState();

    // Battery ADC sample
    if (now - lastBatteryRead_ms >= WS_STATUS_INTERVAL_MS) {
        battery.update();
        lastBatteryRead_ms = now;
    }

    // Status broadcast to browser
    if (now - lastStatusBroadcast_ms >= WS_STATUS_INTERVAL_MS) {
        webServer.setGamepadStatus(gamepad.statusStr(), gamepad.getName());
        webServer.broadcastStatus(
            battery.getPercent(),
            battery.getVoltage(),
            battery.getRatio(),
            battery.getBatteryCells(),
            safety.isSafe(),
            wifi.getIPAddress(),
            motors.isWeaponActive(),
            wifi.getRobotName(),
            motors.isEscReady(),
            motors.escCalibStr(),
            (uint8_t)(motors.getWeaponCurrentSpeed() * 100.0f)
        );
        ble.sendStatus(battery.getPercent(), safety.isSafe(), motors.isWeaponActive());
        lastStatusBroadcast_ms = now;
    }

    // Live controller state to the browser (moves the UI joystick / weapon
    // slider and the Bluetooth tab's controller view). Rate-limited by
    // GAMEPAD_UI_INTERVAL_MS and only sent when the input changed.
    if (now - lastGamepadUi_ms >= GAMEPAD_UI_INTERVAL_MS) {
        lastGamepadUi_ms = now;
        char gpJson[320];
        if (gamepad.takeUiUpdate(gpJson, sizeof(gpJson))) webServer.broadcastText(gpJson);
    }

    // Other robots on the network (scan requested from the UI)
    {
        String robotsJson;
        if (discovery.takeScanResult(robotsJson)) webServer.broadcastText(robotsJson.c_str());
    }

    // Nearby Bluetooth devices (only while a scan requested from the UI runs)
    {
        String nearbyJson;
        if (BtNearby::takeUpdate(nearbyJson, BT_NEARBY_UPDATE_MS)) webServer.broadcastText(nearbyJson.c_str());
    }

    // Bluetooth device details (battery, signal, model…) for the Bluetooth tab
    if (now - lastBtInfo_ms >= BT_INFO_INTERVAL_MS) {
        lastBtInfo_ms = now;
        broadcastBluetoothInfo();
    }

    buzzer.update();
    webServer.update();
    ble.update();
    gamepad.update();
    updateStatusLed();

    // ── Boot button — hold 3s to clear WiFi credentials ───────────────────
    if (digitalRead(PIN_BOOT_BUTTON) == LOW) {
        if (bootBtnPressTime_ms == 0) bootBtnPressTime_ms = now;
        if (!bootBtnHandled && (now - bootBtnPressTime_ms >= 3000)) {
            bootBtnHandled = true;
            Serial.println("[Main] Boot button held 3s — clearing WiFi credentials...");
            buzzer.beepError();
            wifi.clearCredentials();  // saves + reboots
        }
    } else {
        bootBtnPressTime_ms = 0;
        bootBtnHandled      = false;
    }
}
