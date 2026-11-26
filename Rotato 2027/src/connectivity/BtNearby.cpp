// =============================================================================
// BtNearby.cpp — List of Bluetooth LE devices the robot can hear
// =============================================================================
// Copyright (c) 2026 Angel Hernandez (www.thehomelab.dev)
// Non-Commercial Source-Available License — see LICENSE for full terms.
// Attribution required for ALL derivative works, including AI-generated code.

#include "BtNearby.h"
#include "config.h"
#include <ArduinoJson.h>

#if defined(GAMEPAD_ENABLED)

#include <NimBLEDevice.h>

namespace {

constexpr int      MAX_DEVICES = 32;
constexpr uint32_t FORGET_MS   = 60000;   // drop devices not heard for this long

// Services worth naming (bit order = SVC_NAMES order)
constexpr uint16_t SVC_IDS[] = { 0x1812, 0x180F, 0x180A, 0x180D, 0x1816, 0x1818, 0x181A, 0x1802, 0x1803, 0xFE9F, 0xFD6F };
const char* const  SVC_NAMES[] = { "HID", "Battery", "Device Info", "Heart Rate", "Cycling Speed", "Cycling Power",
                                   "Environment", "Alert", "Link Loss", "Google Fast Pair", "Exposure Notification" };
constexpr int SVC_COUNT = sizeof(SVC_IDS) / sizeof(SVC_IDS[0]);

struct Entry {
    char     addr[18];
    char     name[32];
    uint16_t appearance;
    uint16_t company;        // manufacturer data company id (0xFFFF = none)
    uint16_t services;       // bitmask of SVC_IDS
    uint8_t  otherServices;  // advertised services not in SVC_IDS
    int8_t   rssi;
    int8_t   txPower;        // 127 = not advertised
    uint8_t  addrType;       // BLE_ADDR_PUBLIC (0) / RANDOM (1) …
    bool     connectable;
    uint32_t firstSeen_ms, lastSeen_ms;
    uint16_t seen;           // advertisements received
};

Entry         s_devs[MAX_DEVICES];
int           s_count = 0;
portMUX_TYPE  s_mux   = portMUX_INITIALIZER_UNLOCKED;
volatile uint32_t s_scanUntil = 0;
bool          s_wasScanning   = false;
uint32_t      s_lastSend_ms   = 0;

} // namespace

void BtNearby::requestScan(uint32_t ms) {
    uint32_t until = millis() + ms;
    s_scanUntil = until ? until : 1;
}

bool BtNearby::scanWanted() {
    uint32_t until = s_scanUntil;
    return until != 0 && (int32_t)(until - millis()) > 0;
}

void BtNearby::record(const void* p) {
    auto* d = static_cast<const NimBLEAdvertisedDevice*>(p);
    if (!scanWanted()) return;                 // only collect while someone is looking

    // Gather outside the lock (getName() etc. allocate)
    std::string addr = d->getAddress().toString();
    std::string name = d->haveName() ? d->getName() : std::string();
    uint16_t company = 0xFFFF;
    if (d->haveManufacturerData()) {
        std::string m = d->getManufacturerData();
        if (m.size() >= 2) company = (uint8_t)m[0] | ((uint8_t)m[1] << 8);
    }
    uint16_t svc = 0; uint8_t other = 0;
    for (uint8_t i = 0; i < d->getServiceUUIDCount(); i++) {
        NimBLEUUID u = d->getServiceUUID(i);
        int k = 0;
        for (; k < SVC_COUNT; k++) if (u == NimBLEUUID(SVC_IDS[k])) break;
        if (k < SVC_COUNT) svc |= 1u << k; else if (other < 255) other++;
    }
    uint32_t now = millis();

    portENTER_CRITICAL(&s_mux);
    int idx = -1;
    for (int i = 0; i < s_count; i++) if (strcmp(s_devs[i].addr, addr.c_str()) == 0) { idx = i; break; }
    if (idx < 0) {
        if (s_count < MAX_DEVICES) idx = s_count++;
        else {                                  // full: replace the one heard least recently
            idx = 0;
            for (int i = 1; i < s_count; i++) if (s_devs[i].lastSeen_ms < s_devs[idx].lastSeen_ms) idx = i;
        }
        Entry& e = s_devs[idx];
        memset(&e, 0, sizeof(e));
        strlcpy(e.addr, addr.c_str(), sizeof(e.addr));
        e.company = 0xFFFF;
        e.txPower = 127;
        e.firstSeen_ms = now;
    }
    Entry& e = s_devs[idx];
    // Advertisement and scan response carry different fields — merge, don't overwrite with blanks
    if (!name.empty())             strlcpy(e.name, name.c_str(), sizeof(e.name));
    if (d->haveAppearance())       e.appearance = d->getAppearance();
    if (company != 0xFFFF)         e.company = company;
    if (d->haveTXPower())          e.txPower = d->getTXPower();
    e.services     |= svc;
    if (other > e.otherServices)   e.otherServices = other;
    e.rssi          = d->getRSSI();
    e.addrType      = d->getAddressType();
    e.connectable   = e.connectable || d->isConnectable();
    e.lastSeen_ms   = now;
    if (e.seen < 65535) e.seen++;
    portEXIT_CRITICAL(&s_mux);
}

bool BtNearby::takeUpdate(String& json, uint32_t everyMs) {
    bool scanning = scanWanted();
    uint32_t now  = millis();
    if (!scanning && !s_wasScanning) return false;
    if (scanning && s_wasScanning && now - s_lastSend_ms < everyMs) return false;
    if (scanning && !s_wasScanning) {          // new scan — start from an empty list
        portENTER_CRITICAL(&s_mux);
        s_count = 0;
        portEXIT_CRITICAL(&s_mux);
    }
    s_wasScanning = scanning;
    s_lastSend_ms = now;

    // Snapshot under the lock, build JSON outside it
    static Entry snap[MAX_DEVICES];
    portENTER_CRITICAL(&s_mux);
    int n = 0;
    for (int i = 0; i < s_count; i++) if (now - s_devs[i].lastSeen_ms < FORGET_MS) snap[n++] = s_devs[i];
    portEXIT_CRITICAL(&s_mux);

    JsonDocument doc;
    doc["type"]     = "bt_nearby";
    doc["scanning"] = scanning;
    doc["left"]     = scanning ? (int32_t)(s_scanUntil - now) / 1000 : 0;
    JsonArray list  = doc["list"].to<JsonArray>();
    for (int i = 0; i < n; i++) {
        const Entry& e = snap[i];
        JsonObject o = list.add<JsonObject>();
        o["addr"] = e.addr;
        if (e.name[0])         o["name"]    = e.name;
        if (e.appearance)      o["app"]     = e.appearance;
        if (e.company != 0xFFFF) o["company"] = e.company;
        if (e.services || e.otherServices) {
            JsonArray s = o["svc"].to<JsonArray>();
            for (int k = 0; k < SVC_COUNT; k++) if (e.services & (1u << k)) s.add(SVC_NAMES[k]);
            if (e.otherServices) o["svcOther"] = e.otherServices;
        }
        o["rssi"] = e.rssi;
        if (e.txPower != 127)  o["tx"]      = e.txPower;
        o["random"] = e.addrType != 0;
        o["conn"]   = e.connectable;
        o["paired"] = NimBLEDevice::isBonded(NimBLEAddress(std::string(e.addr), e.addrType));
        o["age"]    = (now - e.lastSeen_ms) / 1000;
        o["seen"]   = e.seen;
    }
    json = "";
    serializeJson(doc, json);
    return true;
}

#else // No gamepad task → nobody scans; the list stays empty.

void BtNearby::requestScan(uint32_t) {}
bool BtNearby::scanWanted()          { return false; }
void BtNearby::record(const void*)   {}
bool BtNearby::takeUpdate(String&, uint32_t) { return false; }

#endif
