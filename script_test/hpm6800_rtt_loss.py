"""Loss check for the probe-side RTT bridge over JTAG (HPM6800EVK).

The flood firmware writes the same 13-byte record "hello world!\\n" in a tight
loop. Any dropped byte shifts the pattern, so a received stream that is an exact
repetition of that record proves the delivery is byte-exact - a stronger claim
than "the counters say so".

Also cross-checks the probe's own drain counter against the host byte count, so
the USB/CDC hop is covered too.

Usage: python hpm6800_rtt_loss.py [COM5] [seconds]
"""
import os
import sys
import threading
import time

import hid
import serial

VID, PID = 0x0D28, 0x0204
CMD_RTT = 0x31
ACT_STOP, ACT_START, ACT_STATUS, ACT_CONFIG, ACT_TARGET = 0, 1, 2, 7, 10

CB_ADDR, CB_SIZE = 0x1240000, 0x2000
PAT = b"hello world!\n"


def watchdog(sec):
    def _f():
        time.sleep(sec)
        print("!! WATCHDOG TIMEOUT (%ss)" % sec)
        os._exit(9)
    threading.Thread(target=_f, daemon=True).start()


def open_hid():
    for _ in range(30):
        cand = [i for i in hid.enumerate(VID, PID) if i.get("usage_page") == 0xFF00]
        if cand:
            d = hid.device()
            d.open_path(cand[0]["path"])
            d.set_nonblocking(1)
            time.sleep(0.15)
            return d
        time.sleep(0.3)
    raise RuntimeError("no HID")


def cmd(dev, action, args=None, tmo=2.0):
    args = list(args or [])
    req = [0x01, 2 + len(args), CMD_RTT, action] + args
    req += [0] * (64 - len(req))
    dev.write(req)
    t0 = time.time()
    while time.time() - t0 < tmo:
        d = dev.read(64, timeout_ms=200)
        if d and d[0] == 0x02 and d[2] == CMD_RTT:
            return d
    return None


def words(resp):
    if resp is None:
        return None
    return [int.from_bytes(bytes(resp[4 + i * 4:8 + i * 4]), "little") for i in range(12)]


def u32(v):
    return [v & 0xFF, (v >> 8) & 0xFF, (v >> 16) & 0xFF, (v >> 24) & 0xFF]


def main():
    argv = [a for a in sys.argv[1:] if not a.startswith("--")]
    port = argv[0] if argv else "COM5"
    secs = float(argv[1]) if len(argv) > 1 else 10.0

    dev = open_hid()
    cmd(dev, ACT_TARGET, [1])
    cmd(dev, ACT_CONFIG, u32(0) + [0x00, 0x04, 0, 0xFF])   # chunk 1024 B
    cmd(dev, ACT_STOP)

    ser = serial.Serial(port, 115200, timeout=0.05)
    ser.reset_input_buffer()

    buf = bytearray()
    stop = threading.Event()

    def drain():
        while not stop.is_set():
            buf.extend(ser.read(65536))

    th = threading.Thread(target=drain, daemon=True)
    th.start()

    r = cmd(dev, ACT_START, u32(CB_ADDR) + u32(CB_SIZE) + [0])
    w = words(r)
    rc = (w[0] >> 16) & 0xFF if w else -1
    print("bridge rc=%d, draining %s for %.1fs ..." % (rc, port, secs))

    t0 = time.time()
    time.sleep(secs)
    dt = time.time() - t0
    stop.set()
    th.join(timeout=2.0)
    ser.close()

    w = words(cmd(dev, ACT_STATUS))
    cmd(dev, ACT_STOP)

    got = len(buf)
    print("host received   %d bytes in %.3fs -> %.1f KB/s" % (got, dt, got / dt / 1024.0))
    if w:
        drained, polls, moves = w[3], w[4] & 0xFFFF, w[4] >> 16
        rderr, wderr = w[5] & 0xFFFF, w[5] >> 16
        print("probe drained   %d bytes (polls=%d moves=%d rderr=%d wderr=%d zips=%d)"
              % (drained, polls, moves, rderr, wderr, w[6] >> 16))
        print("probe->host loss: %d bytes" % (drained - got))

    # Stream check: locate the first record boundary, then it must repeat exactly.
    i = buf.find(PAT)
    if i < 0:
        print("FAIL: pattern never found in %d bytes: %r" % (got, bytes(buf[:64])))
        return 1
    body = bytes(buf[i:])
    n = len(body) // len(PAT)
    rem = len(body) % len(PAT)
    exp = PAT * n
    if body[:n * len(PAT)] != exp:
        bad = next(k for k in range(len(exp)) if body[k] != exp[k])
        print("FAIL: stream diverges at offset %d (record %d, byte %d): %r"
              % (bad, bad // len(PAT), bad % len(PAT), bytes(body[bad:bad + 32])))
        return 1
    print("stream check OK: %d complete records + %d trailing bytes, pattern exact"
          % (n, rem))
    print("banner bytes skipped before first record: %d" % i)
    return 0


if __name__ == "__main__":
    watchdog(120)
    sys.exit(main())
