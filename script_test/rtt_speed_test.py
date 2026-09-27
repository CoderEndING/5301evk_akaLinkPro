#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
SEGGER RTT throughput test over akaLinkPro (CMSIS-DAP) + OpenOCD.

Target firmware: script_test/stm32f103_rtt_speed (BLOCK_IF_FIFO_FULL mode,
target writes "hello world!\\n" in a tight loop, so host drain rate == RTT
throughput). OpenOCD finds the RTT control block, streams channel 0 over a
TCP server, and we count bytes for a fixed window per SWD clock speed.

Cross-check afterwards: g_bytes/g_loops/g_ms global variables.
"""
import os
import socket
import subprocess
import sys
import time

# Paths are resolved relative to this file (and the HPM SDK environment) so the
# script works from any clone. Override with OPENOCD_EXE / OPENOCD_SCRIPTS /
# OPENOCD_CFG / FW_BIN / HPM_SDK_ENV_DIR when the tools live elsewhere.
HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
SDK_ENV = os.environ.get("HPM_SDK_ENV_DIR", r"E:\sdk_env_v1.11.0")

OPENOCD = os.environ.get("OPENOCD_EXE", os.path.join(SDK_ENV, "tools", "openocd", "openocd.exe"))
SCRIPTS = os.environ.get("OPENOCD_SCRIPTS", os.path.join(SDK_ENV, "tools", "openocd", "tcl"))
CFG = os.environ.get("OPENOCD_CFG", os.path.join(HERE, "openocd_stm32f1_swd.cfg"))
FW_BIN = os.environ.get("FW_BIN", os.path.join(HERE, "stm32f103_rtt_speed", "build", "fw.bin"))
WORKDIR = HERE

RTT_SERVER_PORT = 21021
TELNET_PORT = 4444
SPEEDS_KHZ = [1000, 10000, 20000, 36000, 60000]
WINDOW_S = 8.0

G_BYTES = 0x20000000
G_LOOPS = 0x20000004
G_MS = 0x20000008


class Telnet:
    def __init__(self, port, timeout=30.0):
        self.sock = socket.create_connection(("127.0.0.1", port), timeout=5)
        self.sock.settimeout(timeout)
        self.buf = b""
        self.read_until(b"> ")

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


def main():
    args = [
        OPENOCD,
        "-s", SCRIPTS,
        "-f", CFG,
        "-c", "gdb port disabled",
        "-c", "tcl port disabled",
        "-c", "telnet port 4444",
    ]
    print("Starting OpenOCD daemon ...")
    proc = subprocess.Popen(args, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True)

    try:
        tn = None
        deadline = time.time() + 20
        while time.time() < deadline:
            try:
                tn = Telnet(TELNET_PORT)
                break
            except OSError:
                time.sleep(0.3)
        if tn is None:
            raise RuntimeError("could not connect to OpenOCD telnet")

        # --- flash the RTT firmware (program = erase + write + verify) ---
        fw = FW_BIN.replace("\\", "/")
        out = tn.cmd(f'program "{fw}" verify 0x08000000')
        if "Verified OK" not in out:
            raise RuntimeError("flash verify failed:\n" + out[-800:])
        print("flash: programmed + verified OK")
        tn.cmd("reset run")
        time.sleep(0.5)

        # --- RTT setup (search whole 20KB RAM for the control block) ---
        print(tn.cmd('rtt setup 0x20000000 0x5000 "SEGGER RTT"').strip())
        print(tn.cmd("rtt start").strip())
        # 1ms poll interval (default 100ms caps the server at ~9 KB/s)
        print(tn.cmd("rtt polling_interval 1").strip())
        print(tn.cmd("rtt server start 21021 0").strip())

        results = []
        for spd in SPEEDS_KHZ:
            tn.cmd(f"adapter speed {spd}")
            label = f"{spd/1000:g} MHz"

            sock = socket.create_connection(("127.0.0.1", RTT_SERVER_PORT), timeout=10)
            total = 0
            polls = 0
            t0 = time.perf_counter()
            sock.settimeout(0.5)
            while True:
                now = time.perf_counter()
                if now - t0 >= WINDOW_S:
                    break
                sock.settimeout(min(0.5, WINDOW_S - (now - t0)))
                try:
                    chunk = sock.recv(65536)
                    if chunk:
                        total += len(chunk)
                        polls += 1
                except socket.timeout:
                    pass
            dur = time.perf_counter() - t0
            sock.close()
            kbps = total / dur / 1024.0
            results.append((label, total, dur, kbps, polls))
            print(f"{label:>8} | {total:9d} bytes in {dur:5.1f}s | {kbps:8.1f} KB/s | {polls} reads")

        # --- cross-check target-side counters ---
        vals = tn.cmd(f"mdw 0x{G_BYTES:X} 3").strip()
        print()
        print("target counters (g_bytes, g_loops, g_ms):")
        print("   ", vals)

        print()
        print("=== summary (RTT ch0, 4KB buffer, host = OpenOCD rtt server) ===")
        print(f"{'SWD clock':>9} | {'throughput':>12}")
        for label, total, dur, kbps, polls in results:
            print(f"{label:>9} | {kbps:9.1f} KB/s")

        try:
            tn.cmd("shutdown")
        except (ConnectionError, OSError):
            pass
        tn.close()
    finally:
        time.sleep(0.5)
        proc.terminate()
        try:
            out = proc.communicate(timeout=5)[0]
        except subprocess.TimeoutExpired:
            proc.kill()
            out = ""
        if proc.returncode not in (0, None):
            print(out[-2000:])


if __name__ == "__main__":
    sys.exit(main())
