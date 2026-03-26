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

void WebServerManager::broadcastStatus(uint8_t batteryPercent, bool safetyOk,
                                       const String& ipAddress, bool weaponActive) {
    if (_ws.count() == 0) return;  // No clients — skip

    // Build JSON status message
    JsonDocument doc;
    doc["battery"] = batteryPercent;
    doc["safety"]  = safetyOk;
    doc["ip"]      = ipAddress;
    doc["weapon"]  = weaponActive;
    doc["fw"]      = FW_VERSION_FULL;

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
    }
}
