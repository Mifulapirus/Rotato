// =============================================================================
// BtDeviceInfo.cpp — Generic information about a connected Bluetooth LE device
// =============================================================================
// Copyright (c) 2026 Angel Hernandez (www.thehomelab.dev)
// Non-Commercial Source-Available License — see LICENSE for full terms.
// Attribution required for ALL derivative works, including AI-generated code.

#include "BtDeviceInfo.h"
#include "config.h"

#if defined(BLE_ENABLED) || defined(GAMEPAD_ENABLED)

#include <NimBLEDevice.h>

// Bluetooth SIG assigned numbers
static constexpr uint16_t SVC_GAP     = 0x1800, SVC_GATT = 0x1801, SVC_DIS = 0x180A,
                          SVC_BATTERY = 0x180F, SVC_HID  = 0x1812;
static constexpr uint16_t CHR_APPEARANCE = 0x2A01, CHR_BATTERY = 0x2A19, CHR_HID_INFO = 0x2A4A,
                          CHR_PNP_ID = 0x2A50, CHR_MODEL = 0x2A24, CHR_SERIAL = 0x2A25,
                          CHR_FW_REV = 0x2A26, CHR_HW_REV = 0x2A27, CHR_SW_REV = 0x2A28,
                          CHR_MANUFACTURER = 0x2A29;

// Read a characteristic if the device has it and allows reading.
static bool readChar(NimBLERemoteService* svc, uint16_t uuid, NimBLEAttValue& out) {
    if (!svc) return false;
    NimBLERemoteCharacteristic* c = svc->getCharacteristic(NimBLEUUID(uuid));
    if (!c || !c->canRead()) return false;
    out = c->readValue();
    return out.length() > 0;
}

// Read a text characteristic, keeping printable ASCII only.
static void readText(NimBLERemoteService* svc, uint16_t uuid, char* buf, size_t len) {
    NimBLEAttValue v;
    if (!readChar(svc, uuid, v)) return;
    size_t n = 0;
    for (size_t i = 0; i < v.length() && n + 1 < len; i++) {
        char ch = (char)v.data()[i];
        if (ch == 0) break;
        if (ch >= 32 && ch < 127 && ch != '"' && ch != '\\') buf[n++] = ch;
    }
    buf[n] = 0;
}

static const char* serviceName(const NimBLEUUID& uuid) {
    static const struct { uint16_t id; const char* name; } KNOWN[] = {
        { SVC_DIS, "Device Info" }, { SVC_BATTERY, "Battery" }, { SVC_HID, "HID" },
        { 0x1813, "Scan Parameters" }, { 0x180D, "Heart Rate" }, { 0x1816, "Cycling" },
        { 0x1819, "Location" }, { 0x181C, "User Data" }, { 0x1802, "Immediate Alert" },
        { 0x1803, "Link Loss" }, { 0x1804, "Tx Power" },
    };
    for (const auto& k : KNOWN)
        if (uuid == NimBLEUUID(k.id)) return k.name;
    return nullptr;
}

static void appendList(char* buf, size_t len, const char* item) {
    if (buf[0]) strlcat(buf, ", ", len);
    strlcat(buf, item, len);
}

// Expects the caller to have discovered all services already (getServices(true)
// BEFORE subscribing to anything) — see GamepadManager::taskLoop().
void BtInfo::readStandard(NimBLEClient* client, BtDeviceInfo& info) {
    if (!client || !client->isConnected()) return;

    NimBLEAddress a = client->getPeerAddress();
    strlcpy(info.addr, a.toString().c_str(), sizeof(info.addr));
    strlcpy(info.addrType, a.isPublic() ? "public"
                         : a.isStatic() ? "random static"
                         : a.isRpa()    ? "random private" : "random", sizeof(info.addrType));

    // Which services the device offers (beyond the mandatory GAP/GATT)
    info.services[0] = 0;
    int vendor = 0;
    // getServices(false): use the list discovered at connect time. NEVER refresh
    // (true) here — that deletes the characteristic objects the device manager
    // subscribed to, and their notifications (e.g. gamepad input) are then dropped.
    for (NimBLERemoteService* svc : client->getServices(false)) {
        const NimBLEUUID& u = svc->getUUID();
        if (u == NimBLEUUID(SVC_GAP) || u == NimBLEUUID(SVC_GATT)) continue;
        const char* n = serviceName(u);
        if (n) appendList(info.services, sizeof(info.services), n);
        else   vendor++;
    }
    if (vendor) {
        char tmp[24];
        snprintf(tmp, sizeof(tmp), "%d vendor-specific", vendor);
        appendList(info.services, sizeof(info.services), tmp);
    }

    NimBLEAttValue v;
    if (readChar(client->getService(NimBLEUUID(SVC_GAP)), CHR_APPEARANCE, v) && v.length() >= 2)
        info.appearance = v.data()[0] | (v.data()[1] << 8);

    NimBLERemoteService* dis = client->getService(NimBLEUUID(SVC_DIS));
    if (dis) {
        readText(dis, CHR_MANUFACTURER, info.manufacturer, sizeof(info.manufacturer));
        readText(dis, CHR_MODEL,        info.model,        sizeof(info.model));
        readText(dis, CHR_SERIAL,       info.serial,       sizeof(info.serial));
        readText(dis, CHR_FW_REV,       info.fwRev,        sizeof(info.fwRev));
        readText(dis, CHR_HW_REV,       info.hwRev,        sizeof(info.hwRev));
        readText(dis, CHR_SW_REV,       info.swRev,        sizeof(info.swRev));
        if (readChar(dis, CHR_PNP_ID, v) && v.length() >= 7) {
            const uint8_t* d = v.data();
            info.hasPnp     = true;
            info.vidSource  = d[0];
            info.vid        = d[1] | (d[2] << 8);
            info.pid        = d[3] | (d[4] << 8);
            info.productVer = d[5] | (d[6] << 8);
        }
    }

    if (readChar(client->getService(NimBLEUUID(SVC_HID)), CHR_HID_INFO, v) && v.length() >= 2)
        info.hidVersion = v.data()[0] | (v.data()[1] << 8);

    readBattery(client, info);
    readLink(client, info);
}

void BtInfo::readBattery(NimBLEClient* client, BtDeviceInfo& info) {
    if (!client || !client->isConnected()) return;
    NimBLEAttValue v;
    if (readChar(client->getService(NimBLEUUID(SVC_BATTERY)), CHR_BATTERY, v))
        info.battery = (int8_t)min<uint8_t>(v.data()[0], 100);
}

void BtInfo::readLink(NimBLEClient* client, BtDeviceInfo& info) {
    if (!client || !client->isConnected()) return;
    NimBLEConnInfo ci = client->getConnInfo();
    info.interval_x4 = ci.getConnInterval() * 5;       // 1.25 ms units → 1/4 ms
    info.latency     = ci.getConnLatency();
    info.timeout_ms  = ci.getConnTimeout() * 10;       // 10 ms units
    info.mtu         = ci.getMTU();
    info.encrypted   = ci.isEncrypted();
    info.bonded      = ci.isBonded();
    info.keySize     = ci.getSecKeySize();
    int r = client->getRssi();
    if (r < 0 && r > -128) info.rssi = (int8_t)r;
}

const char* BtInfo::disconnectReason(int reason) {
    switch (reason & 0xFF) {                           // NimBLE: 0x200 + HCI code
        case 0x08: return "Signal lost (out of range / timeout)";
        case 0x13: return "Device disconnected (turned off?)";
        case 0x16: return "Disconnected by the robot";
        case 0x05: return "Pairing / authentication failed";
        case 0x06: return "Pairing key missing";
        case 0x22:
        case 0x28: return "Link timed out";
        case 0x3B: return "Connection parameters rejected";
        case 0x3D: return "Encryption error";
        case 0x3E: return "Connection could not be established";
        default:   return "Link closed";
    }
}

void BtInfo::addLocal(JsonDocument& doc) {
    if (!NimBLEDevice::isInitialized()) return;
    doc["addr"]     = NimBLEDevice::getAddress().toString();
    doc["maxLinks"] = CONFIG_BT_NIMBLE_MAX_CONNECTIONS;
    JsonArray bonds = doc["bonds"].to<JsonArray>();
    int n = NimBLEDevice::getNumBonds();
    for (int i = 0; i < n; i++) bonds.add(NimBLEDevice::getBondedAddress(i).toString());
}

#else // No Bluetooth stack in this build

void BtInfo::readStandard(NimBLEClient*, BtDeviceInfo&) {}
void BtInfo::readLink(NimBLEClient*, BtDeviceInfo&)     {}
void BtInfo::readBattery(NimBLEClient*, BtDeviceInfo&)  {}
const char* BtInfo::disconnectReason(int)               { return "Link closed"; }
void BtInfo::addLocal(JsonDocument&)                    {}

#endif

void BtInfo::toJson(const BtDeviceInfo& i, JsonObject o) {
    auto str = [&](const char* key, const char* v) { if (v[0]) o[key] = v; };
    o["kind"]      = i.kind;
    o["connected"] = i.connected;
    str("name", i.name);
    str("addr", i.addr);
    str("addrType", i.addrType);
    if (i.appearance) o["appearance"] = i.appearance;
    str("manufacturer", i.manufacturer);
    str("model", i.model);
    str("serial", i.serial);
    str("fw", i.fwRev);
    str("hw", i.hwRev);
    str("sw", i.swRev);
    if (i.hasPnp) {
        o["vidSrc"] = i.vidSource;
        o["vid"]    = i.vid;
        o["pid"]    = i.pid;
        o["pver"]   = i.productVer;
    }
    str("services", i.services);
    if (i.hidVersion) o["hid"] = i.hidVersion;
    if (i.hidReports) o["reports"] = i.hidReports;
    if (i.battery >= 0) o["battery"] = i.battery;
    str("lastDrop", i.lastDrop);
    o["connects"] = i.connects;
    if (!i.connected) return;
    if (i.rssi) o["rssi"] = i.rssi;
    o["interval"] = i.interval_x4 / 4.0f;
    o["latency"]  = i.latency;
    o["timeout"]  = i.timeout_ms;
    o["mtu"]      = i.mtu;
    o["enc"]      = i.encrypted;
    o["bond"]     = i.bonded;
    o["key"]      = i.keySize;
    o["up"]       = (millis() - i.connectedAt_ms) / 1000;
    o["count"]    = i.reportCount;
    o["rate"]     = i.reportRate;
}
