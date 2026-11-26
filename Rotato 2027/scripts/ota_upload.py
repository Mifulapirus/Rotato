"""
ota_upload.py - Upload firmware to a Rotato over WiFi (OTA).

Copyright (c) 2026 Angel Hernandez (www.thehomelab.dev)
Non-Commercial Source-Available License - see LICENSE for full terms.

Used by the [env:d1_mini32_ota] upload_command in platformio.ini, but also
works on its own:

    python scripts/ota_upload.py <firmware.bin> <robot-ip> [expected-version | auto]

    "auto" = expect the version in src/version_build.h (the build just
    compiled). Versions are MAJOR.BUILD ("2.0", "2.1" ...), so every build has
    its own version.

What it does:
  1. POSTs the .bin to http://<robot-ip>/update (the same endpoint the web
     UI's firmware upload uses), printing progress.
  2. The robot verifies the image and reboots. It often reboots before the
     HTTP reply is fully delivered, so a dropped connection AFTER the whole
     file was sent counts as success (that is why plain curl reports an error).
  3. Waits for the robot to come back and, if an expected version was given,
     reads the version it reports over the WebSocket and checks it matches.

Exit code 0 = updated (and verified, when possible); 1 = failed.
Only uses the Python standard library (runs in PlatformIO's own Python).
"""

import base64
import http.client
import json
import os
import socket
import sys
import time
import uuid

CHUNK = 16 * 1024
UPLOAD_TIMEOUT_S = 180
REBOOT_WAIT_S = 60


def log(msg):
    print("[OTA] " + msg, flush=True)


def upload(bin_path, host):
    data = open(bin_path, "rb").read()
    boundary = "----rotato" + uuid.uuid4().hex
    head = (
        "--{b}\r\n"
        'Content-Disposition: form-data; name="firmware"; filename="{f}"\r\n'
        "Content-Type: application/octet-stream\r\n\r\n"
    ).format(b=boundary, f=os.path.basename(bin_path)).encode()
    tail = "\r\n--{b}--\r\n".format(b=boundary).encode()
    total = len(head) + len(data) + len(tail)

    log("Uploading {} ({:,} bytes) to http://{}/update".format(os.path.basename(bin_path), len(data), host))
    conn = http.client.HTTPConnection(host, 80, timeout=UPLOAD_TIMEOUT_S)
    sent = 0
    try:
        conn.putrequest("POST", "/update")
        conn.putheader("Content-Type", "multipart/form-data; boundary=" + boundary)
        conn.putheader("Content-Length", str(total))
        conn.endheaders()
        conn.send(head)
        sent += len(head)
        last_pct = 0
        t0 = time.time()
        for i in range(0, len(data), CHUNK):
            chunk = data[i:i + CHUNK]
            conn.send(chunk)
            sent += len(chunk)
            pct = int(100 * (i + len(chunk)) / len(data))
            if pct >= last_pct + 10 or pct == 100:
                last_pct = pct
                rate = (i + len(chunk)) / max(time.time() - t0, 0.001) / 1024
                log("  {:3d}%  ({:.0f} KB/s)".format(pct, rate))
        conn.send(tail)
        sent += len(tail)
    except (OSError, http.client.HTTPException) as e:
        log("Upload failed after {:,} of {:,} bytes: {}".format(sent, total, e))
        return False

    try:
        resp = conn.getresponse()
        body = resp.read().decode(errors="replace")
        try:
            result = json.loads(body)
        except ValueError:
            result = {"ok": resp.status == 200}
        if not result.get("ok"):
            log("Robot rejected the image: {}".format(result.get("error", body or resp.status)))
            return False
        log("Robot accepted the image - rebooting.")
    except (OSError, http.client.HTTPException):
        # Whole file was delivered; the robot rebooted before its reply arrived.
        log("Image sent; robot closed the connection (it reboots right after accepting).")
    finally:
        conn.close()
    return True


def robot_version(host, timeout=5):
    """Open the robot's WebSocket; return (fw, pcb) from its first status message."""
    s = socket.create_connection((host, 80), timeout=timeout)
    try:
        key = base64.b64encode(os.urandom(16)).decode()
        s.sendall((
            "GET /ws HTTP/1.1\r\nHost: {h}\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
            "Sec-WebSocket-Key: {k}\r\nSec-WebSocket-Version: 13\r\n\r\n"
        ).format(h=host, k=key).encode())
        buf = b""
        while b"\r\n\r\n" not in buf:
            more = s.recv(1024)
            if not more:
                return None
            buf += more
        buf = buf.split(b"\r\n\r\n", 1)[1]
        deadline = time.time() + timeout
        while time.time() < deadline:
            while len(buf) < 2:
                buf += s.recv(4096)
            length = buf[1] & 0x7F
            hdr = 2
            if length == 126:
                while len(buf) < 4:
                    buf += s.recv(4096)
                length = int.from_bytes(buf[2:4], "big")
                hdr = 4
            elif length == 127:
                while len(buf) < 10:
                    buf += s.recv(4096)
                length = int.from_bytes(buf[2:10], "big")
                hdr = 10
            while len(buf) < hdr + length:
                buf += s.recv(4096)
            payload, buf = buf[hdr:hdr + length], buf[hdr + length:]
            try:
                msg = json.loads(payload.decode(errors="replace"))
            except ValueError:
                continue
            if "fw" in msg:
                return msg["fw"], msg.get("pcb")
        return None
    finally:
        s.close()


def wait_for_reboot(host, expected):
    log("Waiting for the robot to come back...")
    time.sleep(3)  # let it actually go down first
    deadline = time.time() + REBOOT_WAIT_S
    while time.time() < deadline:
        try:
            info = robot_version(host)
        except OSError:
            info = None
        if info:
            fw, pcb = info
            if expected and str(fw) != str(expected):
                log("Robot is back but reports firmware {} (expected {}) - the update did NOT apply.".format(fw, expected))
                return False
            log("Robot is back, running firmware {} on PCB rev {}.".format(fw, pcb or "?"))
            return True
        time.sleep(2)
    log("Robot did not come back within {} s - check it (its IP may have changed).".format(REBOOT_WAIT_S))
    return False


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 1
    bin_path, host = sys.argv[1], sys.argv[2]
    expected = sys.argv[3] if len(sys.argv) > 3 else None
    if expected == "auto":
        # Version of the image that was just compiled (written by auto_version.py)
        expected = None
        hdr = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "src", "version_build.h")
        try:
            for line in open(hdr, encoding="utf-8", errors="replace"):
                if line.startswith("#define FW_VERSION_FULL"):
                    expected = line.split('"')[1]
        except OSError:
            pass
    if not upload(bin_path, host):
        return 1
    return 0 if wait_for_reboot(host, expected) else 1


if __name__ == "__main__":
    sys.exit(main())
