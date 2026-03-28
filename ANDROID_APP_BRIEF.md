# Rotato Robot Controller — Android App Brief

This document is a complete specification for building the Rotato Android app in Android Studio.
The app replaces the web UI with a native Android experience, communicating over **Bluetooth Low Energy (BLE)** instead of WebSocket.

---

## 1. Project Context

The robot is a small combat/workshop robot ("Rotato") built around an **ESP32-C3 Super Mini** microcontroller running custom firmware written in C++ (Arduino/PlatformIO). A web UI already exists and works correctly — the Android app should mirror it closely in layout and function, but use BLE as the transport layer.

The firmware already contains a BLE stub (`BLEManager.h/.cpp`) with all UUIDs defined. The Android app developer only needs to build the app side; the firmware will be completed to match this spec.

---

## 2. Hardware Summary

| Item | Detail |
|---|---|
| MCU | ESP32-C3 Super Mini |
| BLE stack (firmware) | NimBLE-Arduino (h2zero/NimBLE-Arduino ^2.2.3) |
| Robot name format | `Rotato-XXXX` where XXXX = last 4 hex chars of MAC |
| Example robot name | `Rotato-36B9` |
| Battery | 2S LiPo, 6.0 V (0%) – 8.4 V (100%) |
| Drive type | Differential / arcade drive (two wheels + optional weapon motor) |
| Safety switch | Hardware kill-switch on J8; when triggered, weapon is blocked |

---

## 3. BLE GATT Profile

### 3.1 Service

| | Value |
|---|---|
| Service UUID | `a1b2c3d4-e5f6-7890-abcd-ef1234567890` |
| Device name (advertised) | `Rotato-XXXX` (matches SSID suffix) |
| Scan filter | Filter by Service UUID — all Rotato robots share the same service UUID |

### 3.2 Characteristics

| Name | UUID | Properties | Data format |
|---|---|---|---|
| DRIVE_X | `a1b2c3d4-e5f6-7890-abcd-ef1234567891` | WRITE NO RESPONSE | 4-byte little-endian IEEE 754 float, range −1.0 … +1.0 |
| DRIVE_Y | `a1b2c3d4-e5f6-7890-abcd-ef1234567892` | WRITE NO RESPONSE | 4-byte little-endian IEEE 754 float, range −1.0 … +1.0 |
| WEAPON  | `a1b2c3d4-e5f6-7890-abcd-ef1234567893` | WRITE NO RESPONSE | 1-byte unsigned int: `0x00` = off, `0x01` = on |
| STATUS  | `a1b2c3d4-e5f6-7890-abcd-ef1234567894` | NOTIFY | JSON UTF-8 string (see §3.3) |

### 3.3 STATUS Notification Payload

The robot sends a STATUS notification at **500 ms intervals** while a client is subscribed. Format:

```json
{"battery":85,"safety":true,"weapon":false,"fw":"1.0.0+42"}
```

| Field | Type | Meaning |
|---|---|---|
| `battery` | integer 0–100 | Battery state of charge (%) |
| `safety` | bool | `true` = weapon allowed; `false` = kill-switch is blocking weapon |
| `weapon` | bool | Current weapon motor state |
| `fw` | string | Firmware version string, e.g. `"1.0.0+42"` |

> **Note:** The `ip` field present in the WebSocket version is omitted from BLE status — it is not relevant when connected over BLE.

### 3.4 Drive Value Encoding

Both DRIVE_X and DRIVE_Y are **4-byte little-endian IEEE 754 floats**.

```kotlin
// Kotlin helper
fun floatToBytes(f: Float): ByteArray {
    val bits = java.lang.Float.floatToIntBits(f)
    return byteArrayOf(
        (bits and 0xFF).toByte(),
        (bits shr 8 and 0xFF).toByte(),
        (bits shr 16 and 0xFF).toByte(),
        (bits shr 24 and 0xFF).toByte()
    )
}
```

### 3.5 Drive Throttling

The joystick should **not** write a new BLE packet on every frame. Send at most **once every 50 ms** (20 Hz). The firmware's motor controller expects frequent updates; if no drive packet arrives for > 500 ms the firmware will not auto-stop (it holds last command), so you should send a `(0, 0)` packet when the user lifts their finger.

### 3.6 Arcade Drive Mixing (for reference)

The firmware applies arcade mixing internally. The app sends raw joystick X/Y; the robot computes:

```
leftSpeed  = Y + X   (clamped to −1.0 … +1.0)
rightSpeed = Y - X   (clamped to −1.0 … +1.0)
```

- **Y positive** → forward
- **X positive** → turn right
- Joystick knob constrained to a circle; values normalized to ±1.0

---

## 4. UI Specification

Mirror the existing web UI as closely as possible using Jetpack Compose (preferred) or XML Views.

### 4.1 Color Palette

| Use | Hex |
|---|---|
| Background | `#1A1A2E` |
| Card / panel background | `#16213E` |
| Accent / primary | `#E94560` |
| Accent light (knob gradient top) | `#FF6B81` |
| Safe / connected green | `#4CAF50` |
| Warning yellow | `#FFC107` |
| Danger / unsafe red | `#E94560` |
| Text primary | `#EEEEEE` |
| Text secondary / labels | `#888888` |
| Inactive borders | `#333333` |

### 4.2 Layout (top to bottom, single scrollable column, max width ~500 dp)

#### Header
- Title: **ROTATO** in large bold text, accent red (`#E94560`), letter-spacing
- Subtitle: `Robot Controller — Asmbly Workshop 2026`, small grey

#### Status Bar (horizontal row, 4 items)
1. **Battery** — progress bar (width = battery%), color thresholds: green >40%, yellow >20%, red ≤20%. Label below shows `XX%`
2. **Safety** — circular indicator dot, green (`#4CAF50`, glowing) when safe, red (`#E94560`, glowing) when blocked
3. **BLE** — circular dot, grey when disconnected, green when connected
4. **FW** — small monospace label showing firmware version received from STATUS

#### Drive (Joystick)
- Section header: `DRIVE` (uppercase, small, grey)
- Circular canvas/composable joystick, ~240×240 dp
  - Outer ring: 2 dp stroke, `#E94560`
  - Crosshair guide lines: `#333333`
  - Knob: filled circle radius ~30 dp, radial gradient `#FF6B81` → `#E94560`, glow shadow
  - Knob is draggable, clamped inside the outer circle
  - On release: snap back to center, send `(0, 0)` drive packet
- Below joystick: small monospace readout `X: 0.00   Y: 0.00`

#### Weapon Motor
- Section header: `WEAPON MOTOR` (uppercase, small, grey)
- Large circular toggle button ~140×140 dp:
  - **Off state:** border `#E94560`, text `#E94560`, background transparent — label `WEAPON\nOFF`
  - **On state:** background `#E94560`, text white, glow box-shadow — label `WEAPON\nON`
  - **Disabled state** (safety blocked): opacity 30%, not tappable, border/text `#555555`
- Warning text below button: `⚠ Safety switch is blocking weapon` — visible only when `safety == false`

#### BLE Info / Settings button
- A subtle button/row: `⚙ BLE Settings` or similar
- Opens a bottom sheet or dialog showing:
  - Connected device name (e.g. `Rotato-36B9`)
  - Disconnect button
  - (No WiFi credential fields — those are web-only)

#### Firmware version footer
- Small monospace text `fw 1.0.0+42`, dim color `#444444`

### 4.3 Scan / Connect Screen

Shown before any robot is connected. Auto-scans for BLE devices advertising the service UUID.

- List of discovered robots (device name + signal strength)
- Tap a robot to connect
- While scanning: show a spinner and `Scanning for robots…`
- While connecting: show `Connecting to Rotato-36B9…`
- On connection failure: show error with a Retry button
- Required Android permissions: `BLUETOOTH_SCAN`, `BLUETOOTH_CONNECT` (Android 12+); `ACCESS_FINE_LOCATION` (Android 11 and below for BLE scan)

---

## 5. BLE Connection Lifecycle

```
App start
  └─► Request BLE permissions
        └─► Scan for service UUID a1b2c3d4-e5f6-7890-abcd-ef1234567890
              └─► User selects robot from list
                    └─► Connect GATT
                          └─► Discover services
                                └─► Get characteristic handles
                                      └─► Enable STATUS notifications (write CCCD 0x0001)
                                            └─► Show controller UI
                                                  │
                                                  ├─► Joystick → write DRIVE_X, DRIVE_Y at 20 Hz
                                                  ├─► Weapon tap → write WEAPON (0x00 or 0x01)
                                                  └─► STATUS notification → update UI
```

**On disconnect:**
- Show a reconnection overlay
- Attempt to reconnect to the same device automatically every 2 seconds
- Stop joystick sending while disconnected
- Send `(0, 0, weapon=off)` before intentional disconnect

---

## 6. Android Project Recommendations

| Decision | Recommendation |
|---|---|
| Language | Kotlin |
| UI framework | Jetpack Compose |
| BLE API | Android `BluetoothLeScanner` + `BluetoothGatt` (standard SDK, no third-party BLE lib required) |
| Architecture | MVVM — `BleRepository` → `RobotViewModel` → Composable screens |
| Min SDK | 26 (Android 8.0) — covers all modern devices, simplifies BLE API |
| Orientation | Portrait only (lock orientation — joystick layout is portrait-first) |
| Keep screen on | Yes — set `WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON` on the controller screen |

### Suggested package structure

```
com.asmbly.rotato/
  ble/
    BleRepository.kt       — scan, connect, GATT callbacks, read/write/notify
    BleDevice.kt           — data class (name, address, rssi)
    RobotStatus.kt         — data class (battery, safety, weapon, fw)
  ui/
    scan/
      ScanScreen.kt
      ScanViewModel.kt
    controller/
      ControllerScreen.kt
      ControllerViewModel.kt
      JoystickView.kt       — custom Composable
      WeaponButton.kt
      StatusBar.kt
  theme/
    Color.kt               — palette from §4.1
    Theme.kt
```

---

## 7. Key Implementation Notes

1. **WRITE NO RESPONSE** — all three command characteristics use `WRITE_TYPE_NO_RESPONSE` (`BluetoothGattCharacteristic.WRITE_TYPE_NO_RESPONSE`). This avoids waiting for acknowledgement and keeps latency low for real-time control.

2. **Enable notifications** — after connecting and discovering services, you must write `0x01, 0x00` to the STATUS characteristic's **Client Characteristic Configuration Descriptor (CCCD)** UUID `00002902-0000-1000-8000-00805f9b34fb`. Standard Android BLE APIs have a built-in helper: `BluetoothGatt.setCharacteristicNotification()` + writing the descriptor manually.

3. **Main thread** — all UI updates from BLE callbacks must be posted to the main thread. Use `Flow`/`StateFlow` in the ViewModel for this.

4. **MTU** — the default MTU (23 bytes) is sufficient for all payloads. STATUS JSON is approximately 50 bytes; request MTU 128 during connection setup (`gatt.requestMtu(128)`) to be safe.

5. **Joystick send loop** — run a `CoroutineScope` with a 50 ms `delay` loop that writes DRIVE_X and DRIVE_Y whenever `joystickActive == true`. Cancel the coroutine on release and send a final `(0f, 0f)`.

6. **Weapon interlock** — mirror the firmware logic: if `safety == false` in the last STATUS packet, disable the weapon button in the UI and do not send weapon-on commands.

7. **Float byte order** — `ByteBuffer.allocate(4).order(ByteOrder.LITTLE_ENDIAN).putFloat(value).array()` is the cleanest way to encode floats on Android.

---

## 8. Permissions (AndroidManifest.xml)

```xml
<!-- BLE hardware requirement -->
<uses-feature android:name="android.hardware.bluetooth_le" android:required="true"/>

<!-- Android 12+ -->
<uses-permission android:name="android.permission.BLUETOOTH_SCAN"
    android:usesPermissionFlags="neverForLocation"/>
<uses-permission android:name="android.permission.BLUETOOTH_CONNECT"/>

<!-- Android 11 and below -->
<uses-permission android:name="android.permission.BLUETOOTH"/>
<uses-permission android:name="android.permission.BLUETOOTH_ADMIN"/>
<uses-permission android:name="android.permission.ACCESS_FINE_LOCATION"/>
```

Request `BLUETOOTH_SCAN` and `BLUETOOTH_CONNECT` at runtime using `ActivityResultContracts.RequestMultiplePermissions`.

---

## 9. What the Firmware Will Do (BLE side)

When `BLE_ENABLED` is defined in `config.h`, the firmware:

- Advertises the service UUID and the robot's name (`Rotato-XXXX`)
- Accepts a single concurrent GATT connection
- On DRIVE_X or DRIVE_Y write: reads the 4-byte float and calls the motor controller immediately
- On WEAPON write: reads the byte and calls `motors.setWeapon(value != 0)`
- Every 500 ms: serializes `{"battery":XX,"safety":bool,"weapon":bool,"fw":"..."}` and notifies STATUS
- On BLE disconnect: sends `stopAll()` to motors (safety)

The firmware will **not** send the `ip` field over BLE (irrelevant in BLE mode).

---

## 10. Updates — Session March 27 2026

The following changes were made to the firmware and web UI after the initial brief was written. The Android app should incorporate or be aware of all of these.

---

### 10.1 BLE Now Fully Implemented (no longer a stub)

`BLEManager.cpp` is fully implemented. The firmware correctly:

- Initialises NimBLE on boot, advertises service UUID + device name
- Accepts DRIVE_X, DRIVE_Y, WEAPON writes
- Notifies STATUS every 500 ms to subscribed clients
- On disconnect: zeros drive outputs immediately (safety)
- Requests tighter connection params on connect: 15–30 ms interval, 0 latency, 2 s supervision timeout

No changes needed to the BLE GATT profile (UUIDs, formats, and payloads remain as specified in §3).

---

### 10.2 BLE Enable/Disable at Runtime

The robot now supports **runtime BLE enable/disable** without rebooting.

- The web settings panel ("Settings → BLE" tab) has a toggle that sends `{"type":"ble_enable","enabled":false}` over WebSocket.
- When disabled: the firmware ignores all incoming BLE drive/weapon commands and immediately zeroes motor outputs; advertising stops.
- When re-enabled: advertising restarts.

**Android app impact:** No new characteristic is needed. The Android app should handle the case where the robot stops responding to BLE writes gracefully (show a "BLE control disabled" notice if STATUS packets continue arriving but drive commands appear to have no effect — this can be detected as a future STATUS field if needed).

---

### 10.3 Drive Modes (Input Transform — App-side)

The web UI now applies **input curve transforms** in the browser before sending X/Y values. The Android app should implement the same transforms client-side, using the user's selected mode.

The firmware always uses the same arcade mixing (`left = Y+X`, `right = Y-X`). The mode curves are applied to the raw joystick values **before** sending.

| Mode | Description | Transform |
|---|---|---|
| **Arcade** (default) | Linear, full bidirectional | `x` and `y` unchanged |
| **Expo** | Precision near center, full authority at edges | `f(v) = 0.5·v³ + 0.5·v` |
| **Precision** | Square-law, very fine low-speed | `f(v) = sign(v)·\|v\|^1.5` |
| **RC Car** | Expo steering, forward-biased throttle | Expo on X; expo on Y with reverse capped at 30% (`if y < 0: y = max(y, -0.3)`) |

Additionally, two pre-transform modifiers are applied first:

1. **Deadzone** (0–25%, default 5%): if `|raw| < dz`, output = 0; otherwise rescale: `out = sign(raw) · (|raw| - dz) / (1 - dz)`
2. **Speed Limit** (10–100%, default 100%): multiply final X and Y by `speedLimit / 100` after mode transform
3. **Invert Y** (bool, default false): negate Y after deadzone, before mode curve

```kotlin
// Kotlin reference implementation
fun applyDeadzone(v: Float, dzFraction: Float): Float {
    if (kotlin.math.abs(v) < dzFraction) return 0f
    val sign = if (v > 0) 1f else -1f
    return sign * (kotlin.math.abs(v) - dzFraction) / (1f - dzFraction)
}

fun expoCurve(v: Float) = 0.5f * v * v * v + 0.5f * v
fun precisionCurve(v: Float) = kotlin.math.sign(v) * kotlin.math.abs(v).pow(1.5f)

fun transformDriveInput(rawX: Float, rawY: Float, settings: DriveSettings): Pair<Float, Float> {
    val dz = settings.deadzonePercent / 100f
    val sl = settings.speedLimitPercent / 100f
    var x = applyDeadzone(rawX, dz)
    var y = applyDeadzone(rawY, dz)
    if (settings.invertY) y = -y
    when (settings.mode) {
        DriveMode.EXPO      -> { x = expoCurve(x);      y = expoCurve(y) }
        DriveMode.PRECISION -> { x = precisionCurve(x); y = precisionCurve(y) }
        DriveMode.RC_CAR    -> { x = expoCurve(x);      if (y < 0) y = maxOf(y, -0.3f) }
        DriveMode.ARCADE    -> { /* no curve */ }
    }
    return Pair(x * sl, y * sl)
}
```

These settings should be **persisted locally** (e.g. `SharedPreferences`) and applied in the joystick send coroutine.

---

### 10.4 Servo / ESC Range (PWM Half-Range)

The firmware now exposes a **runtime-adjustable pulse-width half-range** for drive motors.

**Problem it solves:** Standard ESCs use 1000–2000 µs (±500 µs from neutral 1500 µs). Some ESCs or servos are calibrated for a wider window (e.g. ±700 µs = 800–2200 µs). If the robot only moves ~45° or a fraction of expected travel at full stick, the ESC window is wider than the firmware is sending.

**How it works:**
- Default: `PWM_DRIVE_HALF_RANGE_US = 500` (standard 1000–2000 µs)
- Adjustable at runtime: 200–950 µs in 25 µs steps
- Pulse = `1500 ± halfRange µs`, hard-clamped to `[PWM_MIN_US, PWM_MAX_US]`

**Web UI:** Settings → Drive tab → "Servo / ESC Range" slider. Sends `{"type":"pwm_range","us":600}` over WebSocket on Apply.

**Android app — add a new BLE characteristic OR handle via STATUS:** The current BLE GATT profile has no writable characteristic for this setting. Two options for the Android app:

**Option A (recommended — add a new characteristic):** Add a new GATT write characteristic:

| Name | UUID | Properties | Data format |
|---|---|---|---|
| DRIVE_RANGE | `a1b2c3d4-e5f6-7890-abcd-ef1234567895` | WRITE | 2-byte little-endian uint16, value in µs (e.g. `0xF401` = 500) |

This characteristic has not yet been added to the firmware. If the Android app is being built to support this, request a firmware update to add it.

**Option B (no firmware change):** Implement range only in the app's input transform. Scale the outgoing float values by `(desiredRange / 500)` before writing DRIVE_X/DRIVE_Y, keeping the firmware at its default 500 µs. This avoids needing a new characteristic but means the robot's serial log won't reflect the adjusted range.

For now the **web UI uses Option A-style (sends to firmware)**; the Android app can use **Option B** until the firmware characteristic is added.

---

### 10.5 Channel Renaming (App-side Only)

The web UI allows renaming the "Drive" and "Weapon Motor" section headers. This is **purely cosmetic and stored in `localStorage`** — nothing is sent to the firmware. The Android app should include the same option in its Settings screen, stored in `SharedPreferences`.

---

### 10.6 Settings Screen Recommendation for Android App

Add a Settings bottom sheet or screen with the following sections:

**Drive Settings**
- Control mode: Arcade / Expo / Precision / RC Car (radio buttons / segmented control)
- Speed limit: 10–100% slider
- Deadzone: 0–25% slider
- Invert Y axis: toggle
- Servo/ESC range: 200–950 µs slider (see §10.4 Option B)

**Channel Names**
- Drive label text field
- Weapon label text field

**BLE**
- Note showing connected device name
- Disconnect button
- (BLE enable/disable toggle is web-only; Android app is always the BLE client)
