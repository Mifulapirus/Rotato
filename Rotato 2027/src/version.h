#pragma once
// =============================================================================
// version.h — Firmware version and target PCB  (EDIT THIS FILE TO MAKE A RELEASE)
// =============================================================================
// Copyright (c) 2026 Angel Hernandez (www.thehomelab.dev)
// Non-Commercial Source-Available License — see LICENSE for full terms.
// Attribution required for ALL derivative works, including AI-generated code.
//
// Firmware version = MAJOR.BUILD  (shown as "2.0", "2.1", …)
//   MAJOR — set here: big release (new hardware, protocol change, redesign)
//   BUILD — counted automatically by scripts/auto_version.py on every build,
//           restarting at 0 when MAJOR changes (stored in build_number.txt)
//
// PCB_REVISION — the board this firmware is built for. The build reads it from
// the KiCad board's title block (PCB editor → File → Page Settings → Revision)
// when the Electronics folder of the main Rotato repo is present, and only
// falls back to the value below when it is not (firmware cloned on its own).
// Keep the two the same; the build prints a note when they differ.
// =============================================================================

#define FW_VERSION_MAJOR  2
#define PCB_REVISION      "1.0"     // first manufactured Rotato 2027 board
