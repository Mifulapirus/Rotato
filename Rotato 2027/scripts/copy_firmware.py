import os
import re
import shutil

# PlatformIO post-build script — runs after a successful firmware compile.
# Copies the built firmware.bin to bin/ and names it after the firmware
# version and the PCB revision it was built for:
#
#   rotato-2027-fw<MAJOR>.<BUILD>-pcb<REV>.bin     e.g. rotato-2027-fw2.0-pcb1.0.bin
#
# This makes it easy to drag-and-drop the latest binary into the OTA
# upload page without digging inside .pio/build/.
Import("env")  # noqa: F821 — injected by SCons/PlatformIO


def _read_version(src_dir):
    """Return (firmware version, pcb revision) from the auto-generated version_build.h."""
    header = os.path.join(src_dir, "version_build.h")
    fw, pcb = "0.0", "unknown"
    if os.path.exists(header):
        with open(header, "r", encoding="utf-8", errors="replace") as f:
            content = f.read()
        m = re.search(r'#define\s+FW_VERSION_FULL\s+"([^"]*)"', content)
        if m:
            fw = m.group(1)
        m = re.search(r'#define\s+PCB_REVISION_STR\s+"([^"]*)"', content)
        if m:
            pcb = re.sub(r"[^A-Za-z0-9._-]", "_", m.group(1))
    return fw, pcb


def copy_firmware(source, target, env):  # noqa: ARG001
    project_dir = env.subst("$PROJECT_DIR")
    build_dir   = env.subst("$BUILD_DIR")        # .pio/build/<env>/
    src_dir     = env.subst("$PROJECT_SRC_DIR")

    src_bin = os.path.join(build_dir, "firmware.bin")
    if not os.path.exists(src_bin):
        print("[copy_firmware] WARNING: firmware.bin not found at", src_bin)
        return

    fw, pcb = _read_version(src_dir)
    dest_name   = "rotato-2027-fw{}-pcb{}.bin".format(fw, pcb)
    bin_dir     = os.path.join(project_dir, "bin")
    os.makedirs(bin_dir, exist_ok=True)
    dest_path   = os.path.join(bin_dir, dest_name)

    shutil.copy2(src_bin, dest_path)
    print("\n[copy_firmware] {} -> {}\n".format(src_bin, dest_path))


env.AddPostAction("$BUILD_DIR/firmware.bin", copy_firmware)  # noqa: F821
