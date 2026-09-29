"""J-Scope 探针侧 HSS 采样 —— 端到端验收脚本（不等网页，先在命令行跑通）。

控制面走 HID 0x32（与网页 app/scope/protocol.js 逐字节一致），数据面用 pyusb 直接
读 interface 0 上的 bulk IN **0x83**（网页走 WebUSB，同一根管子）。
CDC/串口桥开关走 HID **0x34**（`--bridge`），也可以让固件在采样期间自己关
（`--flags 0x20` = SCOPE_FLAG_CDC_OFF）。

用法:
  python scope_hss_test.py status [--bridge on|off]
  python scope_hss_test.py bench  [--set pack|cross|one] [--clock 45000000] [--iters 2000]
  python scope_hss_test.py run    [--set pack|cross|one] [--period 100] [--secs 3] [--flags 0x20]

  --flags 位: 0x01 允许 60 MHz / 0x02 丢弃(只采样不推 USB) / 0x08 不让路
              0x10 SWD 空闲拍压 0 / **0x20 采样期间自动关 CDC 桥**
  常用组合: 0x20 = 最快端到端；0x22 = 只量探针本体（丢包就全是探针 CPU 的账）

靶子固件: web-serial-rtt-tools/tools/target-firmware/stm32f103_scope（10 kHz 契约波形）
"""
import sys

# ⚠️ 必须在任何中文 print 之前：GBK 控制台编不出 ⚠️/★ 之类的字符会抛 UnicodeEncodeError，
#    直接把脚本自己打崩（曾经因此把一次正常的 cross 采样误判成固件故障）。
#    原来这段写在 `import sys` **之前**，NameError 被 except 吞掉 → 一直是失效的。
try:
    sys.stdout.reconfigure(encoding="utf-8")
except Exception:
    pass

import argparse
import os
import struct
import threading
import time

import hid
import usb.core
import usb.util

VID, PID = 0x0D28, 0x0204
EP_IN = 0x83
HID_CMD = 0x32
CMD_BRIDGE = 0x34

ACT = {'STOP': 0, 'START': 1, 'STATUS': 2, 'CLOCK': 3, 'TRIGGER': 4,
       'CONFIG': 7, 'BENCH': 8, 'BENCH_RESULT': 9}

BRIDGE = {'STATUS': 0, 'SET': 1}

KIND = {1: 'DEF', 2: 'DATA', 3: 'STAT', 4: 'EVT'}
MAGIC = 0x4A53
PACKET, HEADER, PAYLOAD = 512, 16, 496

TYPES = {0: ('B', 1), 1: ('b', 1), 2: ('H', 2), 3: ('h', 2),
         4: ('I', 4), 5: ('i', 4), 6: ('f', 4), 7: ('d', 8)}

# 靶子固件 stm32f103_scope 的变量契约（tools/target-firmware/stm32f103_scope/README.md）
V_PACK = [  # g_pack 那 8 个字段 → 一个 span（24 B，中间有 2 B 填充）
    ("f_sin",  0x20001014, 4, 6), ("f_tri",  0x20001018, 4, 6),
    ("i_tick", 0x2000101c, 4, 5), ("u_ramp", 0x20001020, 2, 2),
    ("i_sq1k", 0x20001022, 2, 3), ("u_cnt",  0x20001024, 1, 0),
    ("i_saw",  0x20001025, 1, 1), ("u_hi",   0x20001028, 4, 4),
]
V_ONE = [   # 单个 u32（g_tick，10 kHz 斜坡）→ 1 字 span，测"最少传输"下的上限
    ("g_tick", 0x20001044, 4, 4),
]
V_CROSS = [  # span A(0x20000000,14B) + span B(0x20001010,56B)：跨 span 的慢路径
    ("g_lfsr",     0x20000000, 4, 4), ("g_far_cnt",  0x20000008, 4, 4),
    ("g_far_sq100", 0x2000000c, 2, 3), ("g_isr_count", 0x20001010, 4, 4),
    ("i_tick",     0x2000101c, 4, 5), ("g_pair_a",   0x2000102c, 2, 2),
    ("g_pair_b",   0x2000102e, 2, 2), ("g_tick",     0x20001044, 4, 4),
]
V_MIXED = [  # 🚨 混合：一个 3 字 span（自增块读）+ 一个单字 span。
             # 单字快路径（AddrInc=0 + 抱住 TAR）**必须不能启用** —— 一启用就会让 CSW
             # 在自增/不自增之间来回切，切一次赔 2 次传输。这个用例就是守那条守卫的：
             # 采样结果照旧要对（g_tick 斜率），M0 也应该跟"全是单字"明显不同。
    ("g_lfsr",     0x20000000, 4, 4),   # + g_far_cnt @+8 → 并成 0x20000000..0x2000000b 的 3 字 span
    ("g_far_cnt",  0x20000008, 4, 4),
    ("g_tick",     0x20001044, 4, 4),   # 远处孤零零一个字
]
V_TWO = [   # 🚨 两个**远离**的单字 span：都是"4 字节直读"，但地址不同 ⇒ 每拍都得重写 TAR，
            # "抱住 TAR"的前提不成立。守卫必须是 **s_nspans == 1**，不能只是"所有 span
            # 都是单字"：那种情况下 swd_read_word_held 的 TAR 写带一次 RDBUFF 收尾
            # （2 次传输），比 swd_read_block 的裸 TAR 写（1 次）还贵 ⇒ 反而更慢。
    ("g_lfsr",     0x20000000, 4, 4),
    ("g_tick",     0x20001044, 4, 4),
]

# HPM6800EVK（HPM6880，RISC-V/JTAG）靶子 script_test/hpm6800evk_scope：
# 8 × u32 = 32 B，**偏移**在这里，基址用 --base 给（构建后用 nm 查 g_v）。
# 靶子侧波形全部由 g_tick 算出来，所以能逐字段精确反算核对（见 verify_rv()）。
V_RV = [
    ("g_tick",  0, 4, 4),   # 10 kHz 计数（100 µs 一拍）
    ("u_hi",    4, 4, 4),   # 0x10000000 | (t & 0xFFFF)
    ("f_sin",   8, 4, 6),   # 500 Hz 正弦（20 点表）
    ("f_tri",  12, 4, 6),   # 500 Hz 三角
    ("i_sq1k", 16, 4, 5),   # 1 kHz 方波 ±1000
    ("i_sq5k", 20, 4, 5),   # 5 kHz 方波 ±1000（NYQUIST 陷阱）
    ("u_ramp", 24, 4, 4),   # t % 1000
    ("lfsr",   28, 4, 4),   # 32 位 LFSR，每拍一步
]

# 与靶子 src/main.c 里的 kSin20 同一张表（契约的一部分）
RV_SIN20 = [0.0, 0.309017, 0.587785, 0.809017, 0.951057,
            1.0, 0.951057, 0.809017, 0.587785, 0.309017,
            0.0, -0.309017, -0.587785, -0.809017, -0.951057,
            -1.0, -0.951057, -0.809017, -0.587785, -0.309017]


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
            time.sleep(0.2)
            return d
        time.sleep(0.3)
    raise RuntimeError("no HID")


def hid_xfer(dev, data, tmo=3.0):
    """data = [action, ...]；返回**已剥掉 Report ID** 的响应（与网页 xfer() 同语义：
    res[1] === cmd，res[2] 是 payload 第 0 字节）。"""
    req = [0x01, 2 + len(data), HID_CMD] + list(data)
    req += [0] * (64 - len(req))
    dev.write(req)
    t0 = time.time()
    while time.time() - t0 < tmo:
        r = dev.read(64, timeout_ms=200)
        if r and r[0] == 0x02 and r[2] == HID_CMD:
            return list(r)[1:]
    return None


def hid_xfer_raw(dev, cmd, data, tmo=3.0):
    """任意命令号的 HID 收发；返回**已剥掉 Report ID** 的响应。"""
    req = [0x01, 2 + len(data), cmd] + list(data)
    req += [0] * (64 - len(req))
    dev.write(req)
    t0 = time.time()
    while time.time() - t0 < tmo:
        r = dev.read(64, timeout_ms=200)
        if r and r[0] == 0x02 and r[2] == cmd:
            return list(r)[1:]
    return None


def bridge_set(dev, enable):
    """HID 0x34：主循环级 CDC/串口桥开关。返回设置后的实际状态，None = 无响应。"""
    r = hid_xfer_raw(dev, CMD_BRIDGE, [BRIDGE['SET'], 1 if enable else 0])
    if not r:
        return None
    return bool(r[3] & 1)


def bridge_get(dev):
    r = hid_xfer_raw(dev, CMD_BRIDGE, [BRIDGE['STATUS']])
    return None if not r else bool(r[3] & 1)


def s8(v):
    v &= 0xFF
    return v - 256 if v > 127 else v


def status(dev):
    r = hid_xfer(dev, [ACT['STATUS']])
    if not r:
        return None
    w = [int.from_bytes(bytes(r[3 + i * 4:7 + i * 4]), "little") for i in range(12)]
    return {
        'startRc': s8(r[2]), 'running': w[0] & 1, 'riscv': (w[0] >> 1) & 1,
        'spans': (w[0] >> 8) & 0xFF,
        'swdReady': (w[0] >> 16) & 1, 'nvars': (w[0] >> 24) & 0xFF,
        'swdHz': w[1], 'produced': w[2], 'dropped': w[3],
        'swdErr': w[5] & 0xFFFF, 'yield': w[5] >> 16, 'seq': w[6],
        'planHash': w[8], 'periodUs': w[11] & 0xFFFF, 'discard': bool(w[11] & (1 << 16)),
        'swdMhz': (w[11] >> 24) & 0xFF, 'raw': w,
    }


def do_config(dev, period_us, vars_, flags=0):
    d = [ACT['CONFIG']]
    d += list(struct.pack('<I', period_us)) + [flags & 0xFF, len(vars_)]
    for _, addr, size, typ in vars_:
        d += list(struct.pack('<I', addr)) + [size, typ]
    assert len(d) <= 61, "配置报文 %d B 超上限" % len(d)
    r = hid_xfer(dev, d)
    return bool(r)


def do_bench(dev, iters=2000):
    hid_xfer(dev, [ACT['BENCH']] + list(struct.pack('<I', iters)))
    time.sleep(0.35 + iters / 40000.0)
    r = hid_xfer(dev, [ACT['BENCH_RESULT']])
    if not r:
        return None
    ticks = int.from_bytes(bytes(r[3:7]), "little")
    it = int.from_bytes(bytes(r[7:11]), "little")
    err = int.from_bytes(bytes(r[11:15]), "little", signed=True)
    # 字 3/4：**实际装载**的 SWD blob 偏移（见 Custom HID Protocol.md 第 16 条）。
    # 没这个数就分不出"时钟命令被忽略"和"生效了但没差别" —— 这个坑踩过：
    # 1 MHz 与 60 MHz 的读数一模一样，状态字却报着新频率。
    blob = int.from_bytes(bytes(r[15:19]), "little")
    return it, ticks, err, blob


# swd_ops 里的读函数偏移 → 档位（swd_blob_evklite.h 的 SWD_READ_OFFSET_*）
BLOB_TIER = {0x53C: '60M(6 指令/bit)', 0x60C: '45M(8)', 0x6E0: '36M(10)',
             0x7C4: '30M(12)', 0xA54: '20M(18)', 0x620: 'SLOW', 0xFFFFFFFF: '还没装载'}


def find_ep83():
    dev = usb.core.find(idVendor=VID, idProduct=PID)
    if dev is None:
        raise RuntimeError("探针没找到（pyusb）")
    try:
        dev.set_configuration()
    except usb.core.USBError:
        pass
    for intf in dev.get_active_configuration():
        for ep in intf:
            if ep.bEndpointAddress == EP_IN:
                return dev, intf.bInterfaceNumber, ep
    raise RuntimeError("没找到 bulk IN 0x83")


class PktStream:
    """512 B 定长自描述包；按 magic 重同步，坏包只丢自己。"""

    def __init__(self):
        self.buf = bytearray()
        self.pkts = 0
        self.resync = 0

    def push(self, chunk):
        self.buf += chunk
        out = []
        while True:
            if len(self.buf) < PACKET:
                break
            m = int.from_bytes(self.buf[0:2], "little")
            if m != MAGIC:
                i = self.buf.find(struct.pack('<H', MAGIC), 1)
                if i < 0:
                    self.buf = self.buf[-1:]
                    self.resync += 1
                    break
                self.buf = self.buf[i:]
                self.resync += 1
                continue
            p = bytes(self.buf[:PACKET])
            del self.buf[:PACKET]
            self.pkts += 1
            out.append({
                'kind': p[3],
                'seq': int.from_bytes(p[4:8], "little"),
                't_us': int.from_bytes(p[8:12], "little"),
                'n': int.from_bytes(p[12:14], "little"),
                'aux': int.from_bytes(p[14:16], "little"),
                'payload': p[HEADER:],
            })
        return out


def verify_rv(samples):
    """逐字段精确核对 HPM6800EVK scope 靶子的契约。

    靶子的每个字段都是由 `g_tick` 算出来的（见 hpm6800evk_scope/src/main.c），
    而 `g_tick` 与它们**在同一拍**被采到 —— 所以能反算核对，而不是"看着像波形"。
    顺带证明帧内各字段来自同一瞬间（不是拼出来的）。"""
    print("—— 靶子契约核对（每字段都由同一拍的 g_tick 反算）——")
    n = len(samples)
    if n == 0:
        print("  没有样本")
        return False

    # 🚨 先确认靶子真的在动：全 0 的帧会让"f_sin == 表[0] == 0.0"和"u_ramp == 0 % 1000"
    #    这两项**假通过**（实测踩过：探针读回全 0 时它们照样报 100% 正确）。
    ticks = [s['g_tick'] for s in samples]
    if max(ticks) == 0 or len(set(ticks)) == 1:
        print("  靶子数据没在动（g_tick 恒为 %d）—— 先查靶子在不在跑 / 探针读路径（SBCS 配置）"
              % ticks[0])
        return False

    bad = dict.fromkeys(('u_hi', 'f_sin', 'f_tri', 'i_sq1k', 'i_sq5k', 'u_ramp', 'lfsr'), 0)
    lfsr_changes = 0
    prev = None

    for s in samples:
        t = s['g_tick']
        p20 = t % 20
        if s['u_hi'] != (0x10000000 | (t & 0xFFFF)):
            bad['u_hi'] += 1
        if abs(s['f_sin'] - RV_SIN20[p20]) > 1e-6:
            bad['f_sin'] += 1
        if abs(s['f_tri'] - (((p20 if p20 < 10 else 20 - p20) / 10.0) - 1.0)) > 1e-6:
            bad['f_tri'] += 1
        if s['i_sq1k'] != (1000 if (t % 10) < 5 else -1000):
            bad['i_sq1k'] += 1
        if s['i_sq5k'] != (1000 if (t & 1) else -1000):
            bad['i_sq5k'] += 1
        if s['u_ramp'] != (t % 1000):
            bad['u_ramp'] += 1
        if s['lfsr'] == 0:
            bad['lfsr'] += 1
        if prev is not None and s['lfsr'] != prev:
            lfsr_changes += 1
        prev = s['lfsr']

    ok = True
    for k in ('u_hi', 'f_sin', 'f_tri', 'i_sq1k', 'i_sq5k', 'u_ramp', 'lfsr'):
        good = n - bad[k]
        print("  %-7s %6d/%d 正确%s" % (k, good, n, "" if bad[k] == 0 else "   ← %d 个不符" % bad[k]))
        ok = ok and (bad[k] == 0)
    print("  %-7s %6d/%d 逐拍变化%s" % ("lfsr动", lfsr_changes, max(n - 1, 0),
                                     "" if lfsr_changes == max(n - 1, 0) else "   ← 有拍没变"))
    print("  契约核对：%s" % ("PASS" if ok else "FAIL"))
    return ok


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('cmd', choices=['status', 'bench', 'run'])
    ap.add_argument('--set', dest='vset', default='pack', choices=['pack', 'cross', 'one', 'mixed', 'two', 'rv'])
    ap.add_argument('--clock', type=int, default=0, help='SWD Hz，0=不动')
    ap.add_argument('--period', type=int, default=100, help='采样周期 us')
    ap.add_argument('--iters', type=int, default=2000)
    ap.add_argument('--flags', type=lambda s: int(s, 0), default=0, help='flags 位（0x10 = clock_delay 压 0）')
    ap.add_argument('--secs', type=float, default=3.0)
    ap.add_argument('--readsize', type=int, default=8192,
                    help='每次 ep.read 要多少字节。pyusb 是同步读：一个 URB 满了才返回，'
                         '两次 URB 之间的空档里设备发不出东西，所以小读=空档多、大读=空档少但延迟高。'
                         'pack 这种"每包只装 20 个样本"的高包率场景对这个数很敏感。')
    ap.add_argument('--bridge', choices=['on', 'off', 'keep'], default='keep',
                    help='HID 0x34：主循环 CDC/串口桥开关（off = 采样期间不用服务 COM 口，省几百周期/轮）')
    ap.add_argument('--riscv', action='store_true',
                    help='目标类型切 RISC-V/JTAG（HID CMD_RTT action 10）+ 配置里带 flags bit6。'
                         '用于 HPM6800EVK 这类只有 JTAG 的 RISC-V 目标（见 Custom HID Protocol 第 16 条）')
    ap.add_argument('--swd', action='store_true',
                    help='把全局目标类型切回 SWD/ARM（它是粘的：采过 RISC-V 之后要显式切回来）')
    ap.add_argument('--dump', type=int, default=0,
                    help='打印前 N 个解码后的样本值（拿已知内容的地址采，用来验值的正确性）')
    ap.add_argument('--addr', type=lambda s: int(s, 0), default=0,
                    help='覆盖 --set one 那个变量的地址（RISC-V 冒烟测试：随便给个会变的 RAM 地址）')
    ap.add_argument('--base', type=lambda s: int(s, 0), default=0,
                    help='用 8 个连续 u32（base+0..28）替掉整个变量表 —— 量多通道/一个多字 span 用')
    a = ap.parse_args()

    vars_ = {'pack': V_PACK, 'cross': V_CROSS, 'one': V_ONE, 'mixed': V_MIXED, 'two': V_TWO,
             'rv': V_RV}[a.vset]
    if a.vset == 'rv':
        if not a.base:
            print("--set rv 需要 --base <g_v 地址>（构建后 nm 查 g_v，见 hpm6800evk_scope/README.md）")
            return 1
        vars_ = [(n, a.base + off, sz, ty) for (n, off, sz, ty) in V_RV]
    if a.addr and a.vset == 'one':
        vars_ = [("probe_addr", a.addr, 4, 4)]
    if a.base and a.vset != 'rv':
        vars_ = [("w%d" % i, a.base + i * 4, 4, 4) for i in range(8)]
    dev = open_hid()

    if a.riscv:
        # CMD_RTT action 10：全局目标类型（RTT 桥与 J-Scope 共用），0=SWD/ARM，1=RISC-V/JTAG
        r = hid_xfer_raw(dev, 0x31, [10, 1])
        time.sleep(0.05)
        if r is None:
            print("⚠️ CMD_RTT action 10 无响应（固件太旧？）")
        a.flags |= 0x40                     # SCOPE_FLAG_RISCV：强制本会话走 JTAG 后端
        print("目标类型: RISC-V/JTAG（flags 加 bit6）")
    elif a.swd:
        # 把全局目标类型切回 SWD/ARM（它是**粘**的，采过 RISC-V 之后必须显式切回来）
        r = hid_xfer_raw(dev, 0x31, [10, 0])
        time.sleep(0.05)
        if r is None:
            print("⚠️ CMD_RTT action 10 无响应（固件太旧？）")
        print("目标类型: SWD/ARM")

    if a.bridge != 'keep':
        got = bridge_set(dev, a.bridge == 'on')
        if got is None:
            print("⚠️ HID 0x34 无响应（固件太旧？）")
        else:
            print("CDC/串口桥 = %s" % ('开' if got else '关'))

    if a.clock:
        hid_xfer(dev, [ACT['CLOCK']] + list(struct.pack('<I', a.clock)))
        time.sleep(0.1)

    if a.cmd == 'status':
        st = status(dev)
        print(st)
        return 0

    do_config(dev, a.period, vars_, a.flags)
    st = status(dev)
    expect = {'pack': 1, 'one': 1, 'cross': 3, 'mixed': 2, 'two': 2, 'rv': 1}[a.vset]
    print("配置: %d 变量, period=%d us, 探针算出 %d 个 span (本地期望 %s)"
          % (len(vars_), a.period, st['spans'], expect))
    if st['spans'] != expect:
        print("⚠️ span 数与本地计划不一致 —— 检查合并规则/地址")

    if a.cmd == 'bench':
        r = do_bench(dev, a.iters)
        if not r:
            print("标定无响应"); return 1
        it, ticks, err, blob = r
        if err != 0 or it == 0:
            print("标定失败 err=%d iters=%d blob=0x%05X（-3 变量表空 / -4 读失败 / 其它=初始化码）"
                  % (err, it, blob))
            return 1
        us = (ticks / 24.0) / it
        print("**M0 标定**: %d 次 × %.3f us/样本 = %.2f ms  → 上限 ≈ %.1f kHz   [blob=0x%05X %s]"
              % (it, us, ticks / 24.0 / 1000.0, 1000.0 / us, blob, BLOB_TIER.get(blob, '?')))
        return 0

    # ---- run：启动推流 + 读 0x83 ----
    ud, intf, ep = find_ep83()
    try:
        if ud.is_kernel_driver_active(intf):
            ud.detach_kernel_driver(intf)
    except Exception:
        pass
    usb.util.claim_interface(ud, intf)

    st = status(dev)
    dropped0, seq0 = st['dropped'], st['seq']
    stream = PktStream()
    chunks = []
    raw = []
    stop = threading.Event()
    nb = {'bytes': 0}

    # 🚨 读线程必须在 START **之前**起来，否则启动后那段没人读的时间（下面等 rc 的
    #    轮询至少 120 ms）里探针会把包缓冲填满并开始丢拍 —— 实测 period=40us 时
    #    dropped=3192 ≈ 120 ms × 25 kHz − 8 个缓冲，全是这一段的账，看着却像固件丢数据。
    #    同一个坑在 RTT 交付率脚本里已经踩过一次（见 docs/hpm6800evk-jtag.md §5.3）。
    #
    # 🚨 读线程里**只搬字节、不解析**：早先在读线程里直接 PktStream.push()，每个包都建
    #    dict + 496 B 的 bytes 对象，GIL 上与主线程抢，Python 那边一停顿超过
    #    8×512 B 的缓冲深度（pack 组只有 2.1 ms）就真丢数据 —— 而且丢的账会被算到
    #    固件头上（s_usb_drop）。解析挪到窗口结束之后做。
    def reader():
        while not stop.is_set():
            try:
                b = bytes(ep.read(a.readsize, timeout=200))
            except usb.core.USBTimeoutError:
                continue
            except Exception:
                break
            raw.append(b)
            nb['bytes'] += len(b)

    th = threading.Thread(target=reader, daemon=True)
    th.start()

    hid_xfer(dev, [ACT['START']])
    rc = -100
    for _ in range(20):
        time.sleep(0.12)
        st = status(dev)
        rc = st['startRc']
        if rc != -100:
            break
    print("启动 rc=%d (0=ok, -1 时钟, -2 初始化, -3 变量表空, -4 该档不可用)" % rc)
    if rc != 0:
        stop.set()
        usb.util.dispose_resources(ud)
        return 1

    # 从这一刻起才计入速率与丢包统计（启动瞬态已经过去）
    nb['bytes'] = 0
    raw.clear()
    st_begin = status(dev)          # 窗口起点的计数器快照，用来分离「探针丢」与「USB 丢」
    t0 = time.perf_counter()
    time.sleep(a.secs)
    dt = time.perf_counter() - t0
    stop.set()
    th.join(timeout=1.5)
    total = nb['bytes']
    # 🚨 窗口内实收的字节数 —— 只有这一段能算进"端到端速率"。drain 阶段（下面那 0.4 s）
    #    读回来的数据是窗口之后才产生的，混进去会把速率算高：读缓冲开得越大虚高越多
    #    （262144 B 时能虚高到 349 kHz，而探针只产了 339 kHz）。
    win_bytes = total
    st = status(dev)

    hid_xfer(dev, [ACT['STOP']])
    time.sleep(0.15)
    # 停流后把在飞的读收干净
    t1 = time.perf_counter()
    while time.perf_counter() - t1 < 0.4:
        try:
            b = bytes(ep.read(16384, timeout=120))
        except usb.core.USBTimeoutError:
            break
        total += len(b)
        raw.append(b)
    st = status(dev)
    usb.util.dispose_resources(ud)

    # 只解析窗口内那 win_bytes 字节（reader 是按顺序追加的，前 win_bytes 就是窗口内的）
    chunks = stream.push(b"".join(raw)[:win_bytes])
    raw.clear()

    if a.bridge == 'off' or (a.flags & 0x20):
        # 采完就把 COM 口还回去，免得下一次跑脚本时"串口怎么不通了"。
        # flags 0x20（SCOPE_FLAG_CDC_OFF）时这一步应该由固件在 STOP 里做完 —— 这里只是对账。
        bs = bridge_get(dev)
        print("（采样结束后 CDC/串口桥 = %s）" % ('开' if bs else '关'))
        if a.bridge == 'off' and not bs:
            print("（手动恢复 CDC/串口桥 = 开：%s）" % bridge_set(dev, True))

    print("主机收到 %d B / %.2f s -> %.1f KB/s；包数 %d，重同步 %d"
          % (total, dt, total / dt / 1024.0, stream.pkts, stream.resync))

    kinds = {}
    defs = [p for p in chunks if p['kind'] == 1]
    datas = [p for p in chunks if p['kind'] == 2]
    stats = [p for p in chunks if p['kind'] == 3]
    for p in chunks:
        kinds[KIND.get(p['kind'], p['kind'])] = kinds.get(KIND.get(p['kind'], p['kind']), 0) + 1
    print("包类型: %s" % kinds)
    if defs:
        d = defs[0]['payload']
        print("DEF: swd=%d Hz period=%d us flags=0x%X nvars=%d spans=%d"
              % (int.from_bytes(d[0:4], 'little'), int.from_bytes(d[4:8], 'little'),
                 int.from_bytes(d[8:10], 'little'), d[10], d[11]))
    print("probe: produced=%d dropped=%d swdErr=%d yield=%d seq=%d 后端=%s"
          % (st['produced'], st['dropped'], st['swdErr'], st['yield'], st['seq'],
             'RISC-V/JTAG' if st.get('riscv') else 'SWD/ARM'))

    # seq 缺口
    gaps = 0
    prev = None
    for p in sorted(datas + stats, key=lambda x: x['seq']):
        if prev is not None and p['seq'] != prev + 1:
            gaps += p['seq'] - prev - 1
        prev = p['seq']
    print("seq 缺口合计 = %d 包" % gaps)

    # 解码样本 + 按契约核对
    order = sorted(vars_, key=lambda v: v[0] and v[1])   # 固件按地址排序
    order = sorted(vars_, key=lambda v: v[1])
    fb = sum(v[2] for v in order)
    samples = []
    for p in datas:
        nb = min(p['aux'], p['n'] * fb)
        for i in range(nb // fb):
            off = i * fb
            row = {}
            fo = 0
            for name, addr, size, typ in order:
                fmt = '<' + TYPES[typ][0]
                row[name] = struct.unpack_from(fmt, p['payload'], off + fo)[0]
                fo += size
            samples.append(row)
    print("解出 %d 个样本（frame %d B，期望 %d 个/包）" % (len(samples), fb, PAYLOAD // fb))

    if a.dump and samples:
        # 值的正确性只能靠"拿已知内容的地址采"来验（例如靶子里一段常量）：
        #   scope_hss_test.py run --riscv --set one --addr 0x1240000 --dump 6
        for i, s in enumerate(samples[:a.dump]):
            cells = []
            for k, v in s.items():
                cells.append("%s=%d" % (k, v) if isinstance(v, int) else "%s=%.6f" % (k, v))
            print("  样本[%d] = %s" % (i, " ".join(cells)))

    # ★ 端到端速率：窗口内**主机实收**的样本数 ÷ 窗口时长。探针侧的 produced 增量
    #   用来把「探针自己跳拍」和「USB 没送到」分开 —— 两者看着都是掉数据，成因差很远。
    dprod = st['produced'] - st_begin['produced']
    ddrop = st['dropped'] - st_begin['dropped']
    # w4 高 16 位 = s_usb_drop（缓冲耗尽而丢的），低 16 位 = s_bytes。
    # 必须取**窗口增量**：累计值把启动瞬态也算进去，会把结论带偏。
    dusb = (st['raw'][4] >> 16) - (st_begin['raw'][4] >> 16)
    if dt > 0:
        print("★ 端到端 %.1f kHz（主机实收 %d 样本 / %.3f s）" % (len(samples) / dt / 1000.0, len(samples), dt))
        print("  探针窗口内产 %d 拍（%.1f kHz），丢 %d 拍 = %.1f%%（其中 USB 缓冲耗尽 %d 拍 = 丢包的 %.0f%%）"
              % (dprod, dprod / dt / 1000.0, ddrop, 100.0 * ddrop / max(dprod + ddrop, 1),
                 dusb, 100.0 * dusb / max(ddrop, 1)))
    # 校验按"这组里实际有哪些变量"自适应 —— 单选一个 g_tick 时没有 i_tick/u_hi
    if a.vset == 'rv' and samples and not verify_rv(samples):
        return 1
    if len(samples) >= 2:
        tkey = next((k for k in ('i_tick', 'g_tick', 'g_far_cnt') if k in samples[0]), None)
        if tkey is None:
            print("（这组没有 tick 类变量，跳过斜率校验）")
            return 0
        # tick 斜率 = 采样率/目标 tick 率；跳变 > 1 就是丢样本
        ts = [s[tkey] for s in samples]
        d = [ts[i + 1] - ts[i] for i in range(len(ts) - 1)]
        ones = sum(1 for x in d if x == 1)
        jumps = sum(x - 1 for x in d if x > 1)
        print("tick 斜率: 连续+1 占 %d/%d，跳变丢样本合计 %d（probe dropped+usbDrop=%d）"
              % (ones, len(d), jumps, st['dropped']))
        expect_period = (d and sorted(d)[len(d) // 2]) or 0
        print("目标 tick 步进中位数 = %d（=1 表示采样率与目标 10 kHz 同拍）" % expect_period)
        if "u_hi" in samples[0]:
            bad = [s for s in samples if (s["u_hi"] >> 28) != 1]
            print("u_hi 高位校验: %d/%d 正确" % (len(samples) - len(bad), len(samples)))
    return 0


if __name__ == "__main__":
    watchdog(180)
    sys.exit(main())
