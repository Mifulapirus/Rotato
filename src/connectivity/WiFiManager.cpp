// =============================================================================
// WiFiManager.cpp — Access Point + Optional Station Mode (Implementation)
// =============================================================================

#include "WiFiManager.h"

// NVS namespace and keys for storing WiFi credentials + robot identity
static const char* NVS_NAMESPACE    = "robot_wifi";
static const char* NVS_KEY_SSID     = "ssid";
static const char* NVS_KEY_PASS     = "pass";
static const char* NVS_KEY_NAME     = "name";
static const char* NVS_KEY_AP_PASS  = "ap_pass";

void WiFiManager::begin() {
    // Disable saving WiFi config to flash — prevents stale state from
    // a previous sketch or factory firmware from interfering.
    WiFi.persistent(false);
    WiFi.disconnect(true);   // Clear any leftover connection state
    WiFi.mode(WIFI_OFF);     // Start clean
    delay(100);

    // Build SSID now that radio is fresh (macAddress() needs the radio up)
    // We briefly bring radio up just to read MAC, then let begin() set the real mode
    WiFi.mode(WIFI_AP);      // AP mode so macAddress() is reliable on C3
    delay(100);
    _defaultName = buildSSID();  // hardware-derived "Rotato-XXXX", invariant

    // Load custom name + AP password from NVS (fall back to defaults if not set)
    // Open read-write so the namespace is created on the very first boot;
    // read-only (true) would fail with NOT_FOUND on a blank NVS partition.
    _prefs.begin(NVS_NAMESPACE, false);
    String customName = _prefs.getString(NVS_KEY_NAME, "");
    _apPassword = _prefs.getString(NVS_KEY_AP_PASS, AP_PASSWORD);
    _prefs.end();

    _apSSID = customName.isEmpty() ? _defaultName : customName;
    Serial.printf("[WiFi] Robot name  : %s (default: %s)\n", _apSSID.c_str(), _defaultName.c_str());
    Serial.printf("[WiFi] AP password : %s\n", _apPassword.c_str());

    // Try station mode first if we have saved credentials
    if (connectToSavedNetwork()) {
        _mode = RobotWiFiMode::STATION;
    } else {
        startAccessPoint();
        _mode = RobotWiFiMode::ACCESS_POINT;
    }
}

void WiFiManager::saveCredentials(const String& ssid, const String& password) {
    _prefs.begin(NVS_NAMESPACE, false);  // false = read/write mode
    _prefs.putString(NVS_KEY_SSID, ssid);
    _prefs.putString(NVS_KEY_PASS, password);
    _prefs.end();
    Serial.printf("[WiFi] Credentials saved for SSID: %s. Rebooting...\n", ssid.c_str());
    delay(500);
    ESP.restart();  // Reboot to apply new network settings
}

void WiFiManager::clearCredentials() {
    _prefs.begin(NVS_NAMESPACE, false);
    _prefs.clear();
    _prefs.end();
    Serial.println("[WiFi] Credentials cleared. Rebooting into AP mode...");
    delay(500);
    ESP.restart();
}

String WiFiManager::getIPAddress() const {
    if (_mode == RobotWiFiMode::STATION) {
        return WiFi.localIP().toString();
    }
    return String(AP_IP_ADDRESS);
}

// ── Private methods ────────────────────────────────────────────────────────────

bool WiFiManager::connectToSavedNetwork() {
    _prefs.begin(NVS_NAMESPACE, false);  // false = read-write (creates namespace on first boot)
    String ssid = _prefs.getString(NVS_KEY_SSID, "");
    String pass = _prefs.getString(NVS_KEY_PASS, "");
    _prefs.end();

    if (ssid.isEmpty()) {
        Serial.println("[WiFi] No saved credentials. Starting in AP mode.");
        return false;
    }

    Serial.printf("[WiFi] Trying to connect to: %s\n", ssid.c_str());
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), pass.c_str());

    uint32_t startTime = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - startTime > STA_CONNECT_TIMEOUT_MS) {
            Serial.println("[WiFi] Connection timed out. Falling back to AP mode.");
            WiFi.disconnect(true);
            return false;
        }
        delay(200);
        Serial.print(".");
    }

    Serial.printf("\n[WiFi] Connected! IP: %s\n", WiFi.localIP().toString().c_str());
    return true;
}

void WiFiManager::startAccessPoint() {
    Serial.println("[WiFi] Starting Access Point...");

    WiFi.mode(WIFI_AP);
    delay(100);  // Let the radio settle after mode switch

    bool ok = WiFi.softAP(_apSSID.c_str(), _apPassword.c_str());
    delay(500);  // Allow AP to fully initialize before reading config

    if (!ok) {
        // softAP() returned false — radio or config problem
        Serial.println("[WiFi] ERROR: softAP() failed! AP was NOT created.");
        Serial.println("[WiFi]        Check that the password is >= 8 characters.");
        return;
    }

    IPAddress apIP = WiFi.softAPIP();
    Serial.println("[WiFi] ────────────────────────────────────────");
    Serial.printf( "[WiFi]  AP SSID     : %s\n", _apSSID.c_str());
    Serial.printf( "[WiFi]  AP Password : %s\n", _apPassword.c_str());
    Serial.printf( "[WiFi]  AP IP       : %s\n", apIP.toString().c_str());
    Serial.printf( "[WiFi]  Channel     : %d\n", WiFi.channel());
    Serial.printf( "[WiFi]  MAC (AP)    : %s\n", WiFi.softAPmacAddress().c_str());
    Serial.printf( "[WiFi]  TX Power    : %.2f dBm\n", WiFi.getTxPower() * 0.25f);
    Serial.println("[WiFi] ────────────────────────────────────────");
    Serial.println("[WiFi] Open a browser and go to: http://" + apIP.toString());
}

// ── Robot identity ─────────────────────────────────────────────────────────────

bool WiFiManager::saveRobotName(const String& name) {
    // Validate: not empty, not too long, not reserved "Rotato-XXXX" pattern
    if (name.isEmpty() || name.length() > 32) return false;
    if (name.length() == 11 && name.startsWith("Rotato-")) {
        bool hexOk = true;
        for (uint8_t i = 7; i < 11; i++) {
            if (!isxdigit((unsigned char)name[i])) { hexOk = false; break; }
        }
        if (hexOk) {
            Serial.println("[WiFi] saveRobotName: rejected reserved default-format name.");
            return false;
        }
    }
    _prefs.begin(NVS_NAMESPACE, false);
    _prefs.putString(NVS_KEY_NAME, name);
    _prefs.end();
    Serial.printf("[WiFi] Robot name saved: %s. Rebooting...\n", name.c_str());
    delay(500);
    ESP.restart();
    return true;  // unreachable but satisfies compiler
}

void WiFiManager::clearRobotName() {
    _prefs.begin(NVS_NAMESPACE, false);
    _prefs.remove(NVS_KEY_NAME);
    _prefs.end();
    Serial.println("[WiFi] Robot name cleared. Rebooting...");
    delay(500);
    ESP.restart();
}

bool WiFiManager::saveApPassword(const String& pass) {
    if (pass.length() < 8 || pass.length() > 63) {
        Serial.println("[WiFi] saveApPassword: password must be 8-63 characters.");
        return false;
    }
    _prefs.begin(NVS_NAMESPACE, false);
    _prefs.putString(NVS_KEY_AP_PASS, pass);
    _prefs.end();
    Serial.printf("[WiFi] AP password saved. Rebooting...\n");
    delay(500);
    ESP.restart();
    return true;
}

String WiFiManager::buildSSID() {
    // Use the SoftAP MAC address (more stable than station MAC on C3)
    uint8_t mac[6];
    WiFi.softAPmacAddress(mac);
    Serial.printf("[WiFi] Full MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    char suffix[5];
    snprintf(suffix, sizeof(suffix), "%02X%02X", mac[4], mac[5]);
    return String(AP_SSID_PREFIX) + String(suffix);
}
