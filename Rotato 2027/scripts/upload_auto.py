# PlatformIO pre-script: "Upload" picks USB or WiFi by itself.
#
# Copyright (c) 2026 Angel Hernandez (www.thehomelab.dev)
# Non-Commercial Source-Available License - see LICENSE for full terms.
#
# Makes the VS Code "PlatformIO: Upload" button (and plain `pio run -t upload`)
# work with or without the USB cable:
#   - an ESP32 USB-serial adapter is plugged in  -> normal USB flash (esptool)
#   - no adapter found                           -> WiFi upload (scripts/ota_upload.py)
#     to custom_ota_host in platformio.ini
#
# Override for one run:
#   pio run -t upload --upload-port COM5            (USB on that port)
#   pio run -t upload --upload-port 192.168.4.1     (WiFi to that robot)
#
# Environments that set upload_protocol themselves (d1_mini32_ota) are left alone.
import re

Import("env")  # noqa: F821 - injected by SCons/PlatformIO
from SCons.Script import COMMAND_LINE_TARGETS  # noqa: E402

# USB vendor IDs of the USB-serial chips used on ESP32 boards
ESP_USB_VIDS = {
    0x1A86: "CH340/CH9102",
    0x10C4: "CP210x",
    0x0403: "FTDI",
    0x303A: "Espressif USB",
}


def find_serial_port():
    """Return (port, chip name) of the first ESP32-style USB-serial port, or (None, None)."""
    try:
        from serial.tools import list_ports
    except ImportError:
        return None, None
    for p in list_ports.comports():
        if p.vid in ESP_USB_VIDS:
            return p.device, ESP_USB_VIDS[p.vid]
    return None, None


def use_ota(host):
    # The host goes straight into the command and UPLOAD_PORT stays empty: when
    # UPLOAD_PORT is an IP, the espressif32 builder swaps in its own espota
    # uploader, which this firmware doesn't run.
    env.Replace(  # noqa: F821
        UPLOAD_PROTOCOL="custom",
        UPLOAD_PORT="",
        UPLOADCMD='"$PYTHONEXE" "$PROJECT_DIR/scripts/ota_upload.py" "$SOURCE" {} auto'.format(host),
    )
    print("[upload] No USB board found - uploading over WiFi to {}".format(host))


def main():
    if "upload" not in COMMAND_LINE_TARGETS:
        return
    if env.GetProjectOption("upload_protocol", ""):  # noqa: F821 - env chose already
        return

    ota_host = env.GetProjectOption("custom_ota_host", "")  # noqa: F821
    port = env.subst("$UPLOAD_PORT")  # noqa: F821 - from --upload-port / upload_port

    if port:
        # An IP address or hostname means WiFi; anything else (COM5, /dev/ttyUSB0) is USB.
        if re.match(r"^\d+\.\d+\.\d+\.\d+$", port) or port.endswith(".local"):
            use_ota(port)
        else:
            print("[upload] Uploading over USB on {}".format(port))
        return

    serial_port, chip = find_serial_port()
    if serial_port:
        env.Replace(UPLOAD_PORT=serial_port)  # noqa: F821
        print("[upload] USB board found on {} ({}) - uploading over USB".format(serial_port, chip))
    elif ota_host:
        use_ota(ota_host)
    else:
        print("[upload] No USB board found and custom_ota_host is not set - trying USB anyway")


main()
