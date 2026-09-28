"""Flash a firmware image into the HPM6800EVK's QSPI NOR through the akaLinkPro
probe (CMSIS-DAP + OpenOCD), i.e. the same probe that debugs it.

The board's OpenOCD config provides the hpm_xpi flash driver, so no J-Link and
no jumper juggling is needed. The XPI clock is only initialised by the
reset-init event, so the sequence is reset-init -> halt -> write -> verify.

Usage:
  python hpm6800_flash_target.py [image.bin] [--addr 0x80000000] [--no-reset]
"""
import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
SDK_ENV = os.environ.get("HPM_SDK_ENV_DIR", r"E:\sdk_env_v1.11.0")
OPENOCD = os.path.join(SDK_ENV, "tools", "openocd", "openocd.exe")
SCRIPTS = os.path.join(SDK_ENV, "tools", "openocd", "tcl")
SDK_BOARDS = os.path.join(SDK_ENV, "hpm_sdk", "boards", "openocd")
CFG = os.path.join(HERE, "openocd_hpm6800evk_dap.cfg")

DEFAULT_IMAGE = os.path.join(HERE, "hpm6800evk_rtt_flood", "build", "flash_xip", "output", "demo.bin")


def main():
    argv = [a for a in sys.argv[1:]]
    addr = "0x80000000"
    if "--addr" in argv:
        i = argv.index("--addr")
        addr = argv[i + 1]
        del argv[i:i + 2]
    image = argv[0] if argv else DEFAULT_IMAGE
    if not os.path.exists(image):
        raise SystemExit("image not found: %s" % image)
    img = image.replace("\\", "/")

    cmds = [
        "-c", "init",
        "-c", "halt",
        "-c", 'flash write_image erase "%s" %s bin' % (img, addr),
        "-c", 'verify_image "%s" %s bin' % (img, addr),
        "-c", "mdw 0x1200000 1",
    ]
    if "--no-reset" not in sys.argv:
        cmds += ["-c", "reset"]

    args = [OPENOCD, "-s", SCRIPTS, "-s", SDK_BOARDS, "-f", CFG] + cmds
    print("flashing %s -> %s" % (os.path.basename(image), addr))
    t0 = time.time()
    r = subprocess.run(args, capture_output=True, text=True, timeout=180)
    out = (r.stdout or "") + (r.stderr or "")
    for line in out.splitlines():
        if any(k in line for k in ("wrote ", "Verified", "diff", "Error", "error",
                                   "0x01200000", "flash", "Flash")):
            print("  " + line.strip())
    ok = ("wrote " in out) and ("diff" not in out) and ("Error" not in out)
    print("%s in %.1fs" % ("OK" if ok else "FAILED", time.time() - t0))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
