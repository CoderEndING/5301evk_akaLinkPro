#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
SRAM 20KB read/write speed test over akaLinkPro (CMSIS-DAP) + OpenOCD.

Starts OpenOCD as a daemon, connects to the telnet port, and times
load_image (write) / dump_image (read) of a 20KB blob into the
STM32F103 SRAM at 0x20000000 across several SWD clock speeds.
Data integrity is verified per run by comparing the dumped file.
"""
import os
import socket
import subprocess
import sys
import time

# Paths are resolved relative to this file (and the HPM SDK environment) so the
# script works from any clone. Override with OPENOCD_EXE / OPENOCD_SCRIPTS /
# OPENOCD_CFG / HPM_SDK_ENV_DIR when the tools live elsewhere.
HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
SDK_ENV = os.environ.get("HPM_SDK_ENV_DIR", r"E:\sdk_env_v1.11.0")

OPENOCD = os.environ.get("OPENOCD_EXE", os.path.join(SDK_ENV, "tools", "openocd", "openocd.exe"))
SCRIPTS = os.environ.get("OPENOCD_SCRIPTS", os.path.join(SDK_ENV, "tools", "openocd", "tcl"))
CFG = os.environ.get("OPENOCD_CFG", os.path.join(HERE, "openocd_stm32f1_swd.cfg"))
WORKDIR = HERE

SRAM_ADDR = 0x20000000
SIZE = 20 * 1024  # 20480 bytes = full SRAM of STM32F103C8
SPEEDS_KHZ = [1000, 2000, 4000, 10000, 20000, 36000, 45000, 60000]

TELNET_PORT = 4444


class Telnet:
    def __init__(self, port, timeout=30.0):
        self.sock = socket.create_connection(("127.0.0.1", port), timeout=5)
        self.sock.settimeout(timeout)
        self.buf = b""
        self.read_until(b"> ")  # banner + first prompt

    def read_until(self, marker):
        while marker not in self.buf:
            chunk = self.sock.recv(65536)
            if not chunk:
                raise ConnectionError("OpenOCD closed the connection")
            self.buf += chunk
        out, _, self.buf = self.buf.partition(marker)
        return out.decode("utf-8", "replace")

    def cmd(self, c, timeout=120.0):
        self.sock.settimeout(timeout)
        self.sock.sendall((c + "\n").encode())
        return self.read_until(b"> ")

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass


def gen_test_file(path):
    # deterministic pseudo-random content (xorshift), avoids all-0x00/0xFF
    data = bytearray()
    x = 0x12345678
    for _ in range(SIZE):
        x ^= (x << 13) & 0xFFFFFFFF
        x ^= x >> 17
        x ^= (x << 5) & 0xFFFFFFFF
        data.append(x & 0xFF)
    with open(path, "wb") as f:
        f.write(bytes(data))


def main():
    os.makedirs(WORKDIR, exist_ok=True)
    src_bin = os.path.join(WORKDIR, "sram_test.bin")
    dst_bin = os.path.join(WORKDIR, "sram_out.bin")
    gen_test_file(src_bin)
    ref = open(src_bin, "rb").read()
    # OpenOCD telnet/Tcl eats backslashes: always use forward slashes
    src_tel = src_bin.replace("\\", "/")
    dst_tel = dst_bin.replace("\\", "/")

    args = [
        OPENOCD,
        "-s", SCRIPTS,
        "-f", CFG,
        "-c", "gdb port disabled",
        "-c", "tcl port disabled",
        "-c", "telnet port 4444",
    ]
    print("Starting OpenOCD daemon ...")
    proc = subprocess.Popen(args,
                            stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT,
                            text=True)

    try:
        # Wait for the telnet port, but bail out early when OpenOCD dies on its
        # own (usually "cannot read IDR" = no target / target not powered).
        tn = None
        deadline = time.time() + 20
        while time.time() < deadline:
            if proc.poll() is not None:
                out = proc.communicate()[0] or ""
                print(out.strip()[-1500:])
                print()
                if "cannot read IDR" in out:
                    raise RuntimeError(
                        "OpenOCD could not talk to the target DP. Check the "
                        "STM32F103 wiring (SWCLK/SWDIO/GND on the probe's J5), "
                        "target power and BOOT0, then retry.")
                raise RuntimeError("OpenOCD exited early (see its log above)")
            try:
                tn = Telnet(TELNET_PORT)
                break
            except OSError:
                time.sleep(0.3)
        if tn is None:
            raise RuntimeError("could not connect to OpenOCD telnet")

        print(tn.cmd("targets").strip())
        print()

        results = []
        consecutive_fail = 0
        for spd in SPEEDS_KHZ:
            r = tn.cmd(f"adapter speed {spd}")
            label = f"{spd/1000:g} MHz"

            # warmup / baseline small transfer (overhead context)
            ok = True
            tn.cmd("reset halt")

            t0 = time.perf_counter()
            out_w = tn.cmd(f'load_image "{src_tel}" 0x{SRAM_ADDR:X} bin')
            t1 = time.perf_counter()

            out_r = tn.cmd(f'dump_image "{dst_tel}" 0x{SRAM_ADDR:X} {SIZE}')
            t2 = time.perf_counter()

            err = "Error" in out_w or "error" in out_w or "Error" in out_r or "error" in out_r
            data_ok = False
            if not err and os.path.exists(dst_bin):
                got = open(dst_bin, "rb").read()
                data_ok = (got == ref)
            if err or not data_ok:
                print(f"{label:>8} | FAILED (err={err} data_ok={data_ok})")
                consecutive_fail += 1
                if consecutive_fail >= 2:
                    print("two consecutive failures, stopping.")
                    break
                continue
            consecutive_fail = 0

            w_kbps = SIZE / (t1 - t0) / 1024.0
            r_kbps = SIZE / (t2 - t1) / 1024.0
            results.append((label, w_kbps, r_kbps))
            print(f"{label:>8} | write {w_kbps:8.1f} KB/s | read {r_kbps:8.1f} KB/s | verified")

        print()
        print("=== summary (20KB via CMSIS-DAP + OpenOCD load/dump) ===")
        print(f"{'SWD clock':>9} | {'write':>12} | {'read':>12}")
        for label, w, r in results:
            print(f"{label:>9} | {w:10.1f} K/s | {r:10.1f} K/s")

        try:
            tn.cmd("shutdown")
        except (ConnectionError, OSError):
            pass  # OpenOCD exits without a trailing prompt
        tn.close()
    finally:
        time.sleep(0.5)
        proc.terminate()
        try:
            out = proc.communicate(timeout=5)[0] or ""
        except subprocess.TimeoutExpired:
            proc.kill()
            out = ""
        if proc.returncode not in (0, None) and out:
            print(out[-2000:])


if __name__ == "__main__":
    sys.exit(main())
