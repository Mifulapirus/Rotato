#pragma once
// =============================================================================
// BtDeviceInfo.h — Generic information about a connected Bluetooth LE device
// =============================================================================
// Copyright (c) 2026 Angel Hernandez (www.thehomelab.dev)
// Non-Commercial Source-Available License — see LICENSE for full terms.
// Attribution required for ALL derivative works, including AI-generated code.
//
// Device-independent on purpose: today only GamepadManager fills one in, but
// any future Bluetooth device manager (a remote, a sensor, a second controller…)
// reuses the same struct, the same readers and the same JSON. The web UI renders
// every device in the "bt" message the same way, so a new device type shows up
// in the Bluetooth tab without UI changes.
//
// What is read (all optional — a field stays empty if the device lacks it):
//   GAP service          0x1800  Appearance (what kind of device it says it is)
//   Device Information   0x180A  Manufacturer, model, serial, firmware/hardware/
//                                software revision, PnP ID (USB vendor/product)
//   Battery              0x180F  Battery level %
//   HID                  0x1812  HID version, number of input reports
//   Link (from the stack)        Signal (RSSI), connection interval, latency,
//                                supervision timeout, MTU, encryption, bonding
//
// DISCOVERY ORDER: call client->getServices(true) ONCE right after connecting,
// BEFORE subscribing to notifications. readStandard() reuses that list; any
// later refresh deletes the subscribed characteristics and input stops arriving.
//
// THREADING: readStandard() / readLink() / readBattery() do blocking GATT
// requests — call them from a worker task (e.g. the gamepad task), never from
// loop() or a NimBLE callback. Device managers keep their BtDeviceInfo behind a
// lock and hand loop() a copy.
//
// ADDING A NEW DEVICE TYPE:
//   1. In its manager, fill a BtDeviceInfo (kind = "remote", "sensor", …) with
//      BtInfo::readStandard() after connecting and BtInfo::readLink() now and then.
//   2. Give it a  bool getDeviceInfo(BtDeviceInfo& out)  like GamepadManager.
//   3. Add one line to broadcastBluetoothInfo() in main.cpp.
// =============================================================================

#include <Arduino.h>
#include <ArduinoJson.h>

class NimBLEClient;

struct BtDeviceInfo {
    // ── Identity ────────────────────────────────────────────────────────────
    char     kind[12]      = "";      // "gamepad", … (what Rotato uses it as)
    char     name[32]      = "";      // advertised name
    char     addr[18]      = "";      // "aa:bb:cc:dd:ee:ff"
    char     addrType[16]  = "";      // "public", "random static", "random private"
    uint16_t appearance    = 0;       // GAP appearance code (0 = unknown)
    bool     connected     = false;   // false = last device seen (kept for diagnosis)

    // ── Device Information Service ───────────────────────────────────────────
    char     manufacturer[32] = "";
    char     model[32]        = "";
    char     serial[32]       = "";
    char     fwRev[24]        = "";
    char     hwRev[24]        = "";
    char     swRev[24]        = "";
    bool     hasPnp      = false;
    uint8_t  vidSource   = 0;         // 1 = Bluetooth SIG id, 2 = USB vendor id
    uint16_t vid = 0, pid = 0, productVer = 0;

    // ── Services / HID ──────────────────────────────────────────────────────
    char     services[96] = "";       // e.g. "Device Info, Battery, HID"
    uint16_t hidVersion  = 0;         // BCD, 0x0111 = 1.11
    uint8_t  hidReports  = 0;         // input reports subscribed to

    // ── Battery ─────────────────────────────────────────────────────────────
    int8_t   battery     = -1;        // % (-1 = device doesn't report it)

    // ── Link ────────────────────────────────────────────────────────────────
    int8_t   rssi        = 0;         // dBm (0 = unknown)
    uint16_t interval_x4 = 0;         // connection interval in 1/4 ms units (7.5 ms = 30)
    uint16_t latency     = 0;         // peripheral latency (events it may skip)
    uint16_t timeout_ms  = 0;         // supervision timeout
    uint16_t mtu         = 0;
    bool     encrypted   = false;
    bool     bonded      = false;
    uint8_t  keySize     = 0;

    // ── Session stats (filled in by the device manager) ───────────────────────
    uint32_t connectedAt_ms = 0;      // millis() when this connection started
    uint32_t reportCount    = 0;      // input reports this connection
    uint16_t reportRate     = 0;      // input reports per second (recent)
    uint16_t connects       = 0;      // connections since boot
    char     lastDrop[40]   = "";     // why the previous connection ended
};

namespace BtInfo {
    // Read GAP / Device Information / Battery / HID info. Blocking.
    void readStandard(NimBLEClient* client, BtDeviceInfo& info);
    // Refresh RSSI and connection parameters. Fast (no radio round-trip).
    void readLink(NimBLEClient* client, BtDeviceInfo& info);
    // Re-read the battery level. Blocking (one GATT read).
    void readBattery(NimBLEClient* client, BtDeviceInfo& info);

    // Human-readable reason for a NimBLE disconnect code.
    const char* disconnectReason(int reason);

    // Append this device to a JSON array element (keys documented in AI_CONTEXT.md).
    void toJson(const BtDeviceInfo& info, JsonObject obj);
    // Robot-side Bluetooth info: own address, max links, paired (bonded) devices.
    void addLocal(JsonDocument& doc);
}
