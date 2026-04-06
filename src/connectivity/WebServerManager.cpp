// =============================================================================
// WebServerManager.cpp — HTTP Server + WebSocket (Implementation)
// =============================================================================

#include "WebServerManager.h"
#include "version_build.h"

WebServerManager::WebServerManager()
    : _server(WEB_SERVER_PORT), _ws("/ws") {}

void WebServerManager::begin() {
    // ── Mount LittleFS ────────────────────────────────────────────────────────
    // LittleFS is the filesystem we uploaded with "pio run --target uploadfs".
    // It holds index.html and any other static files.
    if (!LittleFS.begin(true)) {
        Serial.println("[Web] ERROR: LittleFS mount failed! Did you upload the filesystem?");
        Serial.println("[Web]        Run: pio run --target uploadfs");
        return;
    }
    Serial.println("[Web] LittleFS mounted.");

    // ── WebSocket setup ──────────────────────────────────────────────────────
    // Register our handler function for WebSocket events (connect, message, disconnect)
    _ws.onEvent([this](AsyncWebSocket* server, AsyncWebSocketClient* client,
                        AwsEventType type, void* arg, uint8_t* data, size_t len) {
        this->onWebSocketEvent(server, client, type, arg, data, len);
    });
    _server.addHandler(&_ws);

    // ── HTTP routes ───────────────────────────────────────────────────────────
    // Serve index.html for all requests to "/" (the root address)
    _server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(LittleFS, "/index.html", "text/html");
    });

    // Serve any other files from LittleFS (CSS, JS, images if added later)
    _server.serveStatic("/", LittleFS, "/");

    // Return a 404 for unknown paths
    _server.onNotFound([](AsyncWebServerRequest* request) {
        request->send(404, "text/plain", "Not found");
    });

    _server.begin();
    Serial.println("[Web] HTTP server started on port 80.");
    Serial.println("[Web] WebSocket listening at /ws");
}

void WebServerManager::broadcastStatus(uint8_t batteryPercent, float batteryVoltage,
                                       float batteryRatio, bool safetyOk,
                                       const String& ipAddress, bool weaponActive,
                                       const String& robotName, bool escReady,
                                       const char* escCalib, uint8_t weaponSpeedPct) {
    if (_ws.count() == 0) return;  // No clients — skip

    // Build JSON status message
    JsonDocument doc;
    doc["battery"]        = batteryPercent;
    doc["batteryV"]       = batteryVoltage;
    doc["batteryRatio"]   = batteryRatio;
    doc["safety"]         = safetyOk;
    doc["ip"]             = ipAddress;
    doc["weapon"]         = weaponActive;
    doc["weaponSpeed"]    = weaponSpeedPct;   // current ramped output 0–100
    doc["fw"]             = FW_VERSION_FULL;
    doc["name"]           = robotName;
    doc["escReady"]       = escReady;
    doc["escCalib"]       = escCalib;
    doc["safety"]       = safetyOk;
    doc["ip"]           = ipAddress;
    doc["weapon"]       = weaponActive;
    doc["fw"]           = FW_VERSION_FULL;
    doc["name"]         = robotName;
    doc["escReady"]     = escReady;
    doc["escCalib"]     = escCalib;

    String json;
    serializeJson(doc, json);
    _ws.textAll(json);
}

void WebServerManager::update() {
    // Required by ESPAsyncWebServer to clean up disconnected clients
    _ws.cleanupClients();
}

// ── Private methods ────────────────────────────────────────────────────────────

void WebServerManager::onWebSocketEvent(AsyncWebSocket* server,
                                         AsyncWebSocketClient* client,
                                         AwsEventType type, void* arg,
                                         uint8_t* data, size_t len) {
    switch (type) {
        case WS_EVT_CONNECT:
            Serial.printf("[Web] WebSocket client #%u connected from %s\n",
                          client->id(), client->remoteIP().toString().c_str());
            break;

        case WS_EVT_DISCONNECT:
            Serial.printf("[Web] WebSocket client #%u disconnected\n", client->id());
            break;

        case WS_EVT_DATA:
            handleWebSocketMessage(arg, data, len);
            break;

        case WS_EVT_ERROR:
            Serial.printf("[Web] WebSocket error #%u: %s\n", client->id(), (char*)data);
            break;

        default:
            break;
    }
}

void WebServerManager::handleWebSocketMessage(void* arg, uint8_t* data, size_t len) {
    AwsFrameInfo* info = (AwsFrameInfo*)arg;

    // Only process complete, single-frame text messages
    if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
        // Null-terminate the data so we can parse it as a string
        data[len] = '\0';

        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, (char*)data);
        if (err) {
            Serial.printf("[Web] JSON parse error: %s\n", err.c_str());
            return;
        }

        const char* type = doc["type"];
        if (!type) return;

        // ── Drive command: {"type":"drive","x":0.5,"y":0.8} ────────────────
        if (strcmp(type, "drive") == 0) {
            float x = doc["x"] | 0.0f;
            float y = doc["y"] | 0.0f;
            if (_driveCallback) _driveCallback(x, y);
        }
        // ── Weapon command: {"type":"weapon","active":true} ─────────────────
        else if (strcmp(type, "weapon") == 0) {
            bool active = doc["active"] | false;
            if (_weaponCallback) _weaponCallback(active);
        }
        // ── Settings command: {"type":"settings","ssid":"Net","pass":"pw"} ──
        else if (strcmp(type, "settings") == 0) {
            String ssid = doc["ssid"] | "";
            String pass = doc["pass"] | "";
            if (!ssid.isEmpty() && _settingsCallback) {
                _settingsCallback(ssid, pass);
            }
        }
        // ── Forget command: {"type":"forget"} ──────────────────────────────────
        else if (strcmp(type, "forget") == 0) {
            Serial.println("[Web] Forget WiFi command received from browser.");
            if (_forgetCallback) _forgetCallback();
        }
        // ── BLE enable/disable: {"type":"ble_enable","enabled":true} ─────────────
        else if (strcmp(type, "ble_enable") == 0) {
            bool enabled = doc["enabled"] | true;
            Serial.printf("[Web] BLE %s by settings panel.\n", enabled ? "enabled" : "disabled");
            if (_bleEnableCallback) _bleEnableCallback(enabled);
        }
        // ── PWM range: {"type":"pwm_range","us":600} ───────────────────────────
        else if (strcmp(type, "pwm_range") == 0) {
            uint16_t us = (uint16_t)(doc["us"] | PWM_DRIVE_HALF_RANGE_US);
            Serial.printf("[Web] PWM range set to ±%u µs by settings panel.\n", us);
            if (_pwmRangeCallback) _pwmRangeCallback(us);
        }        // ── Rename: {"type":"rename","name":"MyRobot"} ("" = reset to default) ──
        else if (strcmp(type, "rename") == 0) {
            String name = doc["name"] | "";
            if (!name.isEmpty()) {
                // Block reserved "Rotato-XXXX" pattern (firmware-side guard)
                bool reserved = false;
                if (name.length() == 11 && name.startsWith("Rotato-")) {
                    bool hexOk = true;
                    for (uint8_t i = 7; i < 11; i++) {
                        if (!isxdigit((unsigned char)name[i])) { hexOk = false; break; }
                    }
                    reserved = hexOk;
                }
                if (reserved) {
                    Serial.println("[Web] Rename rejected: reserved default-format name.");
                    return;
                }
            }
            Serial.printf("[Web] Rename request: \"%s\"\n", name.c_str());
            if (_renameCallback) _renameCallback(name);  // "" = clear custom name
        }
        // ── AP password: {"type":"ap_password","pass":"newpass"} ─────────────
        else if (strcmp(type, "ap_password") == 0) {
            String pass = doc["pass"] | "";
            if (pass.length() < 8 || pass.length() > 63) {
                Serial.println("[Web] AP password rejected: must be 8-63 chars.");
                return;
            }
            Serial.println("[Web] AP password change requested.");
            if (_apPasswordCallback) _apPasswordCallback(pass);
        }
        // ── Battery calibration: {"type":"battery_cal","ratio":3.05} ────────────
        else if (strcmp(type, "battery_cal") == 0) {
            float ratio = doc["ratio"] | 0.0f;
            if (ratio <= 0.0f || ratio > 10.0f) {
                Serial.printf("[Web] Battery ratio %.4f rejected — out of range.\n", ratio);
                return;
            }
            Serial.printf("[Web] Battery calibration: new ratio = %.4f\n", ratio);
            if (_batteryCalibCallback) _batteryCalibCallback(ratio);
        }
        // ── ESC full-range calibration: {"type":"esc_calibrate"} ─────────────
        else if (strcmp(type, "esc_calibrate") == 0) {
            Serial.println("[Web] ESC full-range calibration requested.");
            if (_escCalibrateCallback) _escCalibrateCallback();
        }
        // ── Weapon slider speed: {"type":"weapon_speed","speed":0.75} ────────
        else if (strcmp(type, "weapon_speed") == 0) {
            float speed = doc["speed"] | 0.0f;
            if (speed < 0.0f) speed = 0.0f;
            if (speed > 1.0f) speed = 1.0f;
            if (_weaponSpeedCallback) _weaponSpeedCallback(speed);
        }
        // ── Weapon acceleration: {"type":"weapon_accel","ramp_ms":500,"curve":0} ──
        else if (strcmp(type, "weapon_accel") == 0) {
            uint16_t rampMs = (uint16_t)(doc["ramp_ms"] | (int)WEAPON_RAMP_MS_DEFAULT);
            uint8_t  curve  = (uint8_t) (doc["curve"]   | (int)WEAPON_CURVE_DEFAULT);
            if (curve > 2) curve = 0;
            Serial.printf("[Web] Weapon accel: ramp=%u ms, curve=%u\n", rampMs, curve);
            if (_weaponAccelCallback) _weaponAccelCallback(rampMs, curve);
        }    }
}
