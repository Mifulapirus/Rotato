// =============================================================================
// NetDiscovery.cpp — Find the robot by name on the network (mDNS / Bonjour)
// =============================================================================
// Copyright (c) 2026 Angel Hernandez (www.thehomelab.dev)
// Non-Commercial Source-Available License — see LICENSE for full terms.
// Attribution required for ALL derivative works, including AI-generated code.

#include "NetDiscovery.h"
#include <WiFi.h>
#include <ESPmDNS.h>
#include <ArduinoJson.h>
#include <mdns.h>
#include "version_build.h"

static constexpr uint32_t NAME_CHECK_TIMEOUT_MS = 1500;

String NetDiscovery::toHostname(const String& name) {
    String h;
    bool dash = false;
    for (size_t i = 0; i < name.length() && h.length() < 32; i++) {
        char c = tolower((unsigned char)name[i]);
        if (isalnum((unsigned char)c)) { h += c; dash = false; }
        else if (!dash && h.length()) { h += '-'; dash = true; }
    }
    while (h.endsWith("-")) h.remove(h.length() - 1);
    return h;
}

void NetDiscovery::begin(const String& hostname, const String& uniqueHostname,
                         const String& robotName, const String& defaultName) {
    String unique    = uniqueHostname.length() ? uniqueHostname : String("rotato");
    String preferred = hostname.length() ? hostname : unique;

    // Start on the name that can't clash, then try to claim the preferred one.
    if (!MDNS.begin(unique.c_str())) {
        Serial.println("[mDNS] Failed to start — the robot is reachable by IP only.");
        return;
    }
    _started = true;
    strlcpy(_host, unique.c_str(), sizeof(_host));

    if (preferred != unique) {
        IPAddress other = MDNS.queryHost(preferred.c_str(), NAME_CHECK_TIMEOUT_MS);
        if (other == IPAddress((uint32_t)0)) {
            if (mdns_hostname_set(preferred.c_str()) == ESP_OK)
                strlcpy(_host, preferred.c_str(), sizeof(_host));
        } else {
            _taken = true;
            Serial.printf("[mDNS] %s.local is already used by %s — using %s.local instead.\n",
                          preferred.c_str(), other.toString().c_str(), unique.c_str());
        }
    }

    MDNS.setInstanceName(robotName);              // friendly name in Bonjour browsers
    MDNS.addService("http", "tcp", 80);
    MDNS.addService("rotato", "tcp", 80);         // lets robots find each other
    MDNS.addServiceTxt("rotato", "tcp", "name", robotName);
    MDNS.addServiceTxt("rotato", "tcp", "id",   defaultName);
    MDNS.addServiceTxt("rotato", "tcp", "fw",   FW_VERSION_FULL);
    MDNS.addServiceTxt("rotato", "tcp", "pcb",  PCB_REVISION_STR);

    _robotName = robotName;
    _selfIp = (WiFi.getMode() & WIFI_MODE_STA) ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
    Serial.printf("[mDNS] Open http://%s.local\n", _host);
}

bool NetDiscovery::takeScanResult(String& json) {
    // Only loop() starts scans and reads results, so _result is never read and
    // written at the same time.
    if (_scanRequested && !_scanning && !_ready) {
        _scanRequested = false;
        if (_started) {
            _scanning = true;
            if (xTaskCreate(scanTask, "mdns_scan", 4096, this, 1, nullptr) != pdPASS) _scanning = false;
        }
    }
    if (!_ready) return false;
    json = _result;          // the scan task won't touch _result until the next scan
    _ready = false;
    return true;
}

void NetDiscovery::scanTask(void* arg) {
    auto* self = static_cast<NetDiscovery*>(arg);
    int n = MDNS.queryService("rotato", "tcp");   // blocks ~3 s

    JsonDocument doc;
    doc["type"] = "robots";
    JsonArray list = doc["list"].to<JsonArray>();

    // Always list ourselves (our own advert isn't always in the answers)
    JsonObject me = list.add<JsonObject>();
    me["name"] = self->_robotName;
    me["host"] = String(self->_host) + ".local";
    me["fw"]   = FW_VERSION_FULL;
    me["pcb"]  = PCB_REVISION_STR;
    me["ip"]   = self->_selfIp;
    me["self"] = true;

    for (int i = 0; i < n; i++) {
        String ip = MDNS.IP(i).toString();
        if (ip == self->_selfIp) continue;
        JsonObject r = list.add<JsonObject>();
        r["name"] = MDNS.txt(i, "name");
        r["host"] = MDNS.hostname(i) + ".local";
        r["ip"]   = ip;
        r["fw"]   = MDNS.txt(i, "fw");
        r["pcb"]  = MDNS.txt(i, "pcb");
        r["self"] = false;
    }
    self->_result = "";
    serializeJson(doc, self->_result);
    self->_ready    = true;
    self->_scanning = false;
    vTaskDelete(nullptr);
}
