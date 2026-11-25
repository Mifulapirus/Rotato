// =============================================================================
// WebServerManager.cpp — HTTP Server + WebSocket (Implementation)
// =============================================================================
// Copyright (c) 2026 Angel Hernandez (www.thehomelab.dev)
// Non-Commercial Source-Available License — see LICENSE for full terms.
// Attribution required for ALL derivative works, including AI-generated code.

#include "WebServerManager.h"
#include "version_build.h"
#include "web_ui_html.h"
#include <Update.h>

WebServerManager::WebServerManager()
    : _server(WEB_SERVER_PORT), _ws("/ws") {}

void WebServerManager::begin() {
    // ── Mount LittleFS ────────────────────────────────────────────────────────
    // LittleFS is optional — index.html is now baked into firmware via
    // scripts/embed_web_ui.py so "uploadfs" is no longer required.
    // LittleFS is still mounted so any extra static files placed in data/
    // (images, additional JS) continue to be served without a code change.
    if (!LittleFS.begin(true)) {
        Serial.println("[Web] WARNING: LittleFS mount failed — extra static files unavailable.");
        // Not fatal: the web UI is embedded in firmware and will still work.
    } else {
        Serial.println("[Web] LittleFS mounted (optional static files ready).");
    }

    // ── WebSocket setup ──────────────────────────────────────────────────────
    // Register our handler function for WebSocket events (connect, message, disconnect)
    _ws.onEvent([this](AsyncWebSocket* server, AsyncWebSocketClient* client,
                        AwsEventType type, void* arg, uint8_t* data, size_t len) {
        this->onWebSocketEvent(server, client, type, arg, data, len);
    });
    _server.addHandler(&_ws);

    // ── HTTP routes ───────────────────────────────────────────────────────────
    // Serve the web UI from PROGMEM (gzip-compressed at build time by
    // scripts/embed_web_ui.py).  The browser decompresses automatically
    // when it sees Content-Encoding: gzip.
    _server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
        AsyncWebServerResponse* response = request->beginResponse(
            200, "text/html", WEB_UI_HTML_GZ, WEB_UI_HTML_GZ_LEN);
        response->addHeader("Content-Encoding", "gzip");
        response->addHeader("Cache-Control", "no-cache");
        request->send(response);
    });

    // Serve any other files from LittleFS (CSS, JS, images if added later)
    _server.serveStatic("/", LittleFS, "/");

    // Return a 404 for unknown paths
    _server.onNotFound([](AsyncWebServerRequest* request) {
        request->send(404, "text/plain", "Not found");
    });

    // ── OTA Firmware Update ───────────────────────────────────────────────────
    // POST /update — browser uploads a raw .bin firmware image as multipart.
    // Checks performed:
    //   • Update.begin() confirms an OTA partition exists and there is enough space.
    //   • Each chunk is written and verified by the ESP32 Update library.
    //   • Update.end(true) finalises and validates the MD5 checksum.
    //   • hasError() guards the response: only reboots on a clean image.
    //   • Client-side guards (see index.html): .bin extension, ≥ 4 KB size.
    _server.on("/update", HTTP_POST,
        // ── Response handler (called once upload finishes) ──────────────────
        [](AsyncWebServerRequest* request) {
            bool ok = !Update.hasError();
            String body = ok
                ? "{\"ok\":true}"
                : "{\"ok\":false,\"error\":\"" + String(Update.errorString()) + "\"}";
            AsyncWebServerResponse* resp =
                request->beginResponse(200, "application/json", body);
            resp->addHeader("Connection", "close");
            request->send(resp);
            if (ok) {
                Serial.println("[OTA] Image verified — rebooting to apply update.");
                delay(500);
                ESP.restart();
            } else {
                Serial.printf("[OTA] Update failed: %s\n", Update.errorString());
            }
        },
        // ── Upload handler (called per incoming chunk) ──────────────────────
        [this](AsyncWebServerRequest* request, const String& filename,
           size_t index, uint8_t* data, size_t len, bool final) {
            if (index == 0) {
                Serial.printf("[OTA] Receiving '%s' (content-length: %u bytes)\n",
                              filename.c_str(), request->contentLength());
                // Close all WebSocket clients before allocating the OTA buffer
                // so their heap is reclaimed before Update.begin() runs.
                _ws.closeAll();
                _ws.cleanupClients();
                // U_FLASH = application firmware; UPDATE_SIZE_UNKNOWN lets the
                // library determine the size from the OTA partition boundary.
                if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
                    Serial.printf("[OTA] begin() failed: %s\n", Update.errorString());
                    return;  // hasError() will be true; response handler reports it
                }
            }
            if (!Update.hasError()) {
                if (Update.write(data, len) != len) {
                    Serial.printf("[OTA] write() error: %s\n", Update.errorString());
                }
            }
            if (final) {
                if (Update.end(true)) {  // true = flush remainder + verify MD5
                    Serial.printf("[OTA] Received %u bytes — image OK.\n", index + len);
                } else {
                    Serial.printf("[OTA] end() failed: %s\n", Update.errorString());
                }
            }
        });
    Serial.println("[Web] OTA endpoint registered: POST /update");

    _server.begin();
    Serial.println("[Web] HTTP server started on port 80.");
    Serial.println("[Web] WebSocket listening at /ws");
}

void WebServerManager::broadcastStatus(uint8_t batteryPercent, float batteryVoltage,
                                       float batteryRatio, uint8_t batteryCells,
                                       bool safetyOk, const String& ipAddress,
                                       bool weaponActive, const String& robotName,
                                       bool escReady, const char* escCalib,
                                       uint8_t weaponSpeedPct) {
    if (_ws.count() == 0) return;  // No clients — skip

    // Build JSON status message
    JsonDocument doc;
    doc["battery"]        = batteryPercent;
    doc["batteryV"]       = batteryVoltage;
    doc["batteryRatio"]   = batteryRatio;
    doc["batteryCells"]   = batteryCells;
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
        }        // ── Battery cell count: {"type":"battery_cells","cells":2} ────────────────
        else if (strcmp(type, "battery_cells") == 0) {
            uint8_t cells = (uint8_t)(doc["cells"] | 0);
            if (cells != 2 && cells != 3) {
                Serial.printf("[Web] Battery cells %u rejected — must be 2 or 3.\n", cells);
                return;
            }
            Serial.printf("[Web] Battery type set to %uS\n", cells);
            if (_batteryCellsCallback) _batteryCellsCallback(cells);
        }        // ── ESC full-range calibration: {"type":"esc_calibrate"} ─────────────
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
