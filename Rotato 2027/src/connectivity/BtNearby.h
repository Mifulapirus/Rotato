#pragma once
// =============================================================================
// BtNearby.h — List of Bluetooth LE devices the robot can hear
// =============================================================================
// Copyright (c) 2026 Angel Hernandez (www.thehomelab.dev)
// Non-Commercial Source-Available License — see LICENSE for full terms.
// Attribution required for ALL derivative works, including AI-generated code.
//
// Answers "can Rotato connect to this device?" before any driver exists for it:
// if a device shows up here it speaks Bluetooth LE (Classic-only devices such
// as speakers or PS4 controllers never appear).
//
// HOW IT WORKS:
//   - Whoever owns the NimBLE scan (today the gamepad task) passes EVERY
//     advertisement to BtNearby::record(). It runs on the NimBLE host task and
//     only copies a few fields into a fixed table under a spinlock.
//   - requestScan() asks the scan owner to keep scanning for a while even when
//     it otherwise wouldn't (e.g. while the controller is connected).
//   - loop() calls takeUpdate() about once a second while a scan is running
//     and broadcasts the JSON to the browser.
//
// A device only advertises when it is on and looking for a connection — put
// mice / keyboards / controllers in pairing mode to see them.
// =============================================================================

#include <Arduino.h>

namespace BtNearby {
    // Ask for a scan of `ms` milliseconds. Safe from any task.
    void requestScan(uint32_t ms);
    // True while a requested scan is running (the scan owner polls this).
    bool scanWanted();

    // Record one advertisement (called by the scan owner from its NimBLE
    // scan callback). `advertisedDevice` is a const NimBLEAdvertisedDevice*.
    void record(const void* advertisedDevice);

    // loop(): returns true at most once per `everyMs` while scanning (and once
    // more when the scan ends), filling json with
    //   {"type":"bt_nearby","scanning":true,"left":12,"list":[{…}, …]}
    // See AI_CONTEXT.md for the per-device fields.
    bool takeUpdate(String& json, uint32_t everyMs);
}
