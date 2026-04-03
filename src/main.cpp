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

// ── Boot button state (GPIO9, hold 3s to clear WiFi credentials) ─────────────
static uint32_t bootBtnPressTime_ms = 0;
static bool     bootBtnHandled      = false;

// ── Setup ─────────────────────────────────────────────────────────────────────
void setup() {
    // ── 1. Serial ────────────────────────────────────────────────────────────
    Serial.begin(115200);

    // ESP32-C3 Super Mini uses USB-CDC — wait up to 5s for monitor to connect.
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
    Serial.println("  Weaponized Plastic — 2026");
    Serial.println("========================================\n");
    Serial.println("[Boot] Serial OK");
    Serial.printf("[Boot] Firmware v%s  (built %s %s)\n", FW_VERSION_FULL, FW_BUILD_DATE, FW_BUILD_TIME);

    // ── Chip info ────────────────────────────────────────────────────────────
    {
        static const char* const FM_NAMES[] = { "QIO","QOUT","DIO","DOUT","Fast","Slow" };
        uint8_t  fm  = (uint8_t)ESP.getFlashChipMode();
        uint64_t mac = ESP.getEfuseMac();
        Serial.println("[Boot] ────────────────────────────────────────");
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
        Serial.println("[Boot] ────────────────────────────────────────");
    }

    // ── 2. Hardware drivers ──────────────────────────────────────────────────
    motors.begin();
    battery.begin();
    buzzer.begin();
    safety.begin();

    // Boot button — active LOW, internal pull-up
    pinMode(PIN_BOOT_BUTTON, INPUT_PULLUP);

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

    webServer.onForget([]() {
        wifi.clearCredentials();
    });

    webServer.onBleEnable([](bool en) {
        ble.setEnabled(en);
    });

    webServer.onPwmRange([](uint16_t halfRangeUs) {
        motors.setDriveRange(halfRangeUs);
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

    webServer.onApPassword([](const String& pass) {
        wifi.saveApPassword(pass);  // saves + reboots
    });

    webServer.onBatteryCalib([](float ratio) {
        battery.setRatio(ratio);
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

    // Battery ADC sample
    if (now - lastBatteryRead_ms >= WS_STATUS_INTERVAL_MS) {
        battery.update();
        lastBatteryRead_ms = now;
    }

    // Status broadcast to browser
    if (now - lastStatusBroadcast_ms >= WS_STATUS_INTERVAL_MS) {
        webServer.broadcastStatus(
            battery.getPercent(),
            battery.getVoltage(),
            battery.getRatio(),
            safety.isSafe(),
            wifi.getIPAddress(),
            motors.isWeaponActive(),
            wifi.getRobotName()
        );
        ble.sendStatus(battery.getPercent(), safety.isSafe(), motors.isWeaponActive());
        lastStatusBroadcast_ms = now;
    }

    buzzer.update();
    webServer.update();
    ble.update();

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
