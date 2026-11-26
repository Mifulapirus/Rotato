#pragma once
// =============================================================================
// NetDiscovery.h — Find the robot by name on the network (mDNS / Bonjour)
// =============================================================================
// Copyright (c) 2026 Angel Hernandez (www.thehomelab.dev)
// Non-Commercial Source-Available License — see LICENSE for full terms.
// Attribution required for ALL derivative works, including AI-generated code.
//
// ADDRESS:  http://<robot-name>.local   (e.g. "Rotato-7C1A" → rotato-7c1a.local,
//           a robot renamed "Rotato" → rotato.local)
//
// SEVERAL ROBOTS ON ONE NETWORK:
//   - The default name ends in the MAC address, so default addresses never clash.
//   - With a custom name, the robot first asks the network whether that address
//     is already taken (another robot with the same name, or any other device).
//     If it is, it keeps its unique default address instead of fighting over it
//     (the UI shows which address it ended up with).
//   - Every robot advertises a "_rotato._tcp" service, so each robot's web UI
//     can list the others ("Robots on this network") — scan() below.
//   - It also advertises "_http._tcp", so generic Bonjour/mDNS browser apps show it.
//
// THREADING: begin() runs once in setup() (blocks up to ~1.5 s for the name
// check). requestScan() may be called from any task; the query runs on its own
// short-lived task (it blocks ~3 s) and loop() collects the result with
// takeScanResult().
// =============================================================================

#include <Arduino.h>

class NetDiscovery {
public:
    // hostname        — preferred name (from the robot name), without ".local"
    // uniqueHostname  — fallback that can't clash (from the default Rotato-XXXX name)
    void begin(const String& hostname, const String& uniqueHostname,
               const String& robotName, const String& defaultName);

    // Hostname actually in use, e.g. "rotato-7c1a" ("" before begin()).
    const char* hostname() const { return _host; }
    // True if the preferred hostname was taken by another device.
    bool preferredTaken() const { return _taken; }

    // Turn any robot name into a valid hostname: lowercase letters, digits and
    // single dashes, max 32 characters. "Blade Runner!" → "blade-runner".
    static String toHostname(const String& name);

    // Ask for a scan for other robots. Safe from any task (e.g. a WebSocket
    // callback); the scan itself is started by takeScanResult() on loop().
    void requestScan() { _scanRequested = true; }
    // Call every loop(): starts a requested scan, and returns true once per
    // finished scan with json =
    //   {"type":"robots","list":[{"name","host","ip","fw","self"}…]}
    bool takeScanResult(String& json);

private:
    char          _host[33] = "";
    bool          _taken    = false;
    bool          _started  = false;
    String        _selfIp;
    String        _robotName;
    volatile bool _scanRequested = false;
    volatile bool _scanning = false;
    volatile bool _ready    = false;
    String        _result;           // written by the scan task, read by loop() after _ready

    static void scanTask(void* arg);
};
