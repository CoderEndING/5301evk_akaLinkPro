"""raw DAP 通道的边界回归测试（对应 docs/代码审查报告.md 的处置项 #3 / #6）。

修复前的三个问题，这个脚本就是守它们的：

  1. **响应缓冲溢出**：raw 通道的 `s_raw_rsp` 只有 24 字节，而 DAP_TransferBlock 的
     count 是**请求里声明的 2 字节字段** —— 一条 5 字节请求声明 count=0xFFFF，响应
     就是 4 + 4×count ≈ 256 KB，直接写穿邻接静态区。现在缓冲是 `DAP_XFER_SIZE`
     （1024 B），并在入口把 count 钳到 255。
  2. **响应长度不受请求长度限定的嵌套**：ExecuteCommands(0x7F) 会把嵌套命令的响应
     叠起来、还能再套一层。raw 通道的语义就是"发一条命令"，现在直接拒绝，回一个
     `DAP_ERROR`（长度 1）。
  3. **混合"读+写"的 DAP_Transfer**：收上一笔 AP 读结果那一步必须走**读引擎**
     （`SWD_Read(DP_RDBUFF|RnW)`）；修复前那一处写成了 `SWD_Write`，于是回给主机的
     是未初始化/陈旧的值。这里拿 ROM 里两个**恒定且已知**的字（0x08000000 的初始
     栈指针、0x08000004 的复位向量）当基准来验。

只读目标内存（唯一的写是 DP ABORT 写 0，无副作用），对靶子固件没有要求。
HID 取回的响应被 api_param 截到 16 字节，所以大 count 的用例只核对包头里的
count/status 字段与前几个字 —— 关键是**探针不能死**。

用法：python dap_raw_boundary_test.py
"""
import os
import sys
import time
import threading

import hid

try:                                    # Windows 控制台默认 GBK，中文/符号会直接抛异常
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception:
    pass

CMD_RTT = 0x31
ACT_RAW_DAP = 4
ACT_RAW_RESULT = 6

ID_DAP_CONNECT = 0x02
ID_DAP_TRANSFER = 0x05
ID_DAP_TRANSFER_BLOCK = 0x06
ID_DAP_SWJ_SEQUENCE = 0x12
ID_DAP_EXECUTE_COMMANDS = 0x7F

APnDP, RnW = 0x01, 0x02
CSW_VAL = 0x23000052          # 32 位 + AddrInc=自增（与 rtt_rawdap.py 同一取值）
SP_ADDR = 0x08000000          # 初始栈指针（ROM，恒定）
VTOR_ADDR = 0x08000004        # 复位向量（ROM，恒定）

FAILS = []


def watchdog(sec):
    def _f():
        time.sleep(sec)
        print("!! WATCHDOG TIMEOUT")
        os._exit(9)
    threading.Thread(target=_f, daemon=True).start()


def open_hid():
    for _ in range(30):
        cand = [i for i in hid.enumerate(0x0D28, 0x0204) if i.get("usage_page") == 0xFF00]
        if cand:
            d = hid.device()
            d.open_path(cand[0]["path"])
            d.set_nonblocking(1)
            time.sleep(0.2)
            return d
        time.sleep(0.3)
    raise RuntimeError("no custom HID interface")


def xfer(dev, pkt, tmo=2.0):
    pkt = list(pkt)
    pkt += [0] * (64 - len(pkt))
    dev.write(pkt)
    t0 = time.time()
    while time.time() - t0 < tmo:
        r = dev.read(64, timeout_ms=200)
        if r and r[0] == 0x02 and r[2] == CMD_RTT:
            return list(r)
    return None


def raw(dev, req, note):
    """跑一条 raw DAP 请求，返回 (n, payload)；None = 探针没回。"""
    r0 = xfer(dev, [0x01, 0x01, CMD_RTT, ACT_RAW_DAP, len(req)] + list(req))
    if r0 is None:
        print("  %-34s [FAIL] 排队写入无响应" % note)
        return None
    time.sleep(0.05)                      # 主循环执行它
    r = xfer(dev, [0x01, 0x01, CMD_RTT, ACT_RAW_RESULT])
    if r is None:
        print("  %-34s [FAIL] 结果读回无响应" % note)
        return None
    n = r[3]
    payload = bytes(r[4:4 + min(n, 16)])
    return n, payload


def check(cond, note, detail=""):
    print("  %-34s %-5s %s" % (note, "[ok]" if cond else "[FAIL]", detail))
    if not cond:
        FAILS.append(note)
    return cond


def summary(payload):
    """DAP_Transfer 响应 = [echo][count][status][data...]；
    TransferBlock 响应 = [echo][count_lo][count_hi][status][data...]（count 是 2 字节）。"""
    if len(payload) < 3:
        return None, None, []
    if payload[0] == ID_DAP_TRANSFER_BLOCK:
        count = payload[1] | (payload[2] << 8)
        status = payload[3] if len(payload) > 3 else None
        head = 4
    else:
        count = payload[1]
        status = payload[2] if len(payload) > 2 else None
        head = 3
    words = [int.from_bytes(payload[head + i * 4:head + 4 + i * 4], "little")
             for i in range((len(payload) - head) // 4)]
    return count, status, words


def acked(payload):
    """ACK 位：OK=1 / WAIT=2 / FAULT=4；7 = 总线上没人应答（SWDIO 常高）。"""
    _, status, _ = summary(payload)
    return status is not None and (status & 0x01) != 0 and (status & 0x06) == 0


def set_tar(dev, addr):
    return raw(dev, [ID_DAP_TRANSFER, 0x00, 0x01, APnDP | 0x04,
                     addr & 0xFF, (addr >> 8) & 0xFF, (addr >> 16) & 0xFF, (addr >> 24) & 0xFF],
               "TAR = 0x%08X" % addr)


def bring_up(dev):
    """raw 通道**不做链路初始化** —— 它假定探针自己那一侧（RTT 桥 / scope）已经把
    SWD 链路带起来了。所以这里只做一次 IDCODE 读当"链路活着"的判据。

    ⚠️ 不要在这里发 SWJ_Sequence 的 line reset：本机实测（见 --diag）51 拍 line
    reset 之后 IDCODE 就读不到了（ACK=7），再发一次 jtag-to-swd 才恢复 —— 这条
    序列路径有问题，已单独记录。"""
    print("=== 1. 链路检查（探针侧应已初始化过 SWD）===")
    r = raw(dev, [ID_DAP_TRANSFER, 0x00, 0x01, 0x02], "DP read IDCODE")
    ok = r is not None and acked(r[1])
    _, status, words = summary(r[1]) if r else (None, None, [])
    check(ok, "IDCODE 有 ACK", "status=%s idcode=0x%08X" % (status, words[0] if words else 0))
    if not ok:
        print("  ⇒ 链路没起来。先跑一次 scope 或桥的初始化，例如：")
        print("     python scope_hss_test.py bench --set one --clock 60000000")
        return False
    r = raw(dev, [ID_DAP_TRANSFER, 0x00, 0x01, APnDP | 0x00,
                  CSW_VAL & 0xFF, (CSW_VAL >> 8) & 0xFF, (CSW_VAL >> 16) & 0xFF, (CSW_VAL >> 24) & 0xFF],
            "AP write CSW")
    check(r is not None and acked(r[1]), "CSW 写入 ACK",
          "status=%s" % (summary(r[1])[1] if r else None))
    return True


def diag(dev):
    """逐步定位：每一步之后 IDCODE 还读得到吗（判断是哪一步把链路搞坏的）。"""
    print("=== 诊断：逐步加回 bring-up，看 IDCODE 什么时候开始读不到 ===")

    def idcode():
        r = raw(dev, [ID_DAP_TRANSFER, 0x00, 0x01, 0x02], "  → DP read IDCODE")
        _, status, words = summary(r[1]) if r else (None, None, [])
        return "status=%s idcode=0x%08X" % (status, words[0] if words else 0)

    print("0) 裸读（什么都不做）      %s" % idcode())
    raw(dev, [ID_DAP_CONNECT, 0x01], "1) DAP_Connect(SWD)")
    print("   Connect 之后             %s" % idcode())
    raw(dev, [ID_DAP_SWJ_SEQUENCE, 0x01, 51, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x07], "2) line reset 51")
    print("   line reset 之后          %s" % idcode())
    raw(dev, [ID_DAP_SWJ_SEQUENCE, 0x01, 16, 0x9E, 0xE7], "3) jtag-to-swd")
    print("   jtag-to-swd 之后         %s" % idcode())
    raw(dev, [ID_DAP_SWJ_SEQUENCE, 0x01, 51, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x07], "4) line reset 51")
    print("   再 line reset 之后       %s" % idcode())
    return 0


def main():
    if "--diag" in sys.argv:
        dev = open_hid()
        return diag(dev)

    dev = open_hid()
    if not bring_up(dev):
        return 1

    print("\n=== 2. 基准：ROM 里两个恒定的字 ===")
    set_tar(dev, SP_ADDR)
    r = raw(dev, [ID_DAP_TRANSFER_BLOCK, 0x00, 0x02, 0x00, APnDP | RnW | 0x0C], "block read x2")
    count, status, words = summary(r[1]) if r else (None, None, [])
    if not check(r is not None and acked(r[1]) and len(words) >= 2, "读到 2 个字",
                 "count=%s status=%s words=%s" % (count, status, ["0x%08X" % w for w in words])):
        print("靶子不可读，后续用例没法判读 —— 先修靶子。")
        return 1
    sp, vtor = words[0], words[1]
    check((sp >> 24) == 0x20 or (sp >> 24) == 0x08, "第一个字像栈指针/向量", "0x%08X" % sp)

    print("\n=== 3. #3：raw 通道的大 count TransferBlock（旧缓冲 24 B，4+4×count 会溢出）===")
    for n in (8, 64):
        set_tar(dev, SP_ADDR)
        r = raw(dev, [ID_DAP_TRANSFER_BLOCK, 0x00, n & 0xFF, (n >> 8) & 0xFF, APnDP | RnW | 0x0C],
                "block read x%d" % n)
        count, status, words = summary(r[1]) if r else (None, None, [])
        check(r is not None and count == n and acked(r[1]),
              "count=%d 回读正常" % n,
              "count=%s status=%s 首字=0x%08X(期望 0x%08X)" % (count, status, words[0] if words else 0, sp))
        check(bool(words) and words[0] == sp, "count=%d 数据与基准一致" % n)

    set_tar(dev, SP_ADDR)
    r = raw(dev, [ID_DAP_TRANSFER_BLOCK, 0x00, 0xFF, 0xFF, APnDP | RnW | 0x0C],
            "block read x0xFFFF（应被钳到 255）")
    count, status, words = summary(r[1]) if r else (None, None, [])
    check(r is not None and count is not None and count <= 255 and acked(r[1]),
          "count=0xFFFF 被钳位", "count=%s status=%s" % (count, status))

    print("\n=== 4. #3：ExecuteCommands(0x7F) 应被拒绝 ===")
    r = raw(dev, [ID_DAP_EXECUTE_COMMANDS, 0x01, 0x01, ID_DAP_TRANSFER, 0x00, 0x01, 0x02],
            "ExecuteCommands（嵌套）")
    n, payload = (r[0], r[1]) if r else (None, b"")
    check(n == 1 and payload[:1] == b"\xFF", "回一个 DAP_ERROR", "n=%s payload=%s" % (n, payload.hex()))

    print("\n=== 5. #6：混合“读 + 写”的 DAP_Transfer（收上一笔读必须走读引擎）===")
    # item0 = 读 AP DRW（自增），item1 = 写 DP ABORT（写 0，无副作用）。
    # 语义（DAP.c:1001-1024 + 1060-1078）：**首个** AP 读只"投递"读请求、不产出数据，
    # 并把 post_read 置 1；跟着的那个写分支才去收 DP_RDBUFF —— 也就是被修成
    # SWD_Write 的那一句。所以正常响应里只有 **1 个**数据字，且必须等于 TAR 处的内存字
    # （这里 TAR=0x08000004 ⇒ 复位向量）。修复前那句是写引擎，根本没收，回的是
    # 栈上的陈旧值。注意 HID 回读固定给 16 字节，后面的 0 是补零不是数据。
    for i in (0, 1):
        set_tar(dev, VTOR_ADDR)
        # ⚠️ DAP_Transfer 的 count 是 **1 字节**（TransferBlock 才是 2 字节），
        #    item 紧跟其后：item0 = 读 AP DRW，item1 = 写 DP ABORT(=0，无副作用)。
        r = raw(dev, [ID_DAP_TRANSFER, 0x00, 0x02,
                      APnDP | RnW | 0x0C,
                      0x00, 0x00, 0x00, 0x00, 0x00],
                "混合读+写 #%d" % (i + 1))
        count, status, words = summary(r[1]) if r else (None, None, [])
        check(r is not None and count == 2 and acked(r[1]), "混合传输 ACK",
              "count=%s status=%s words=%s" % (count, status, ["0x%08X" % w for w in words]))
        check(bool(words) and words[0] == vtor, "收回来的值 = 复位向量",
              "0x%08X（期望 0x%08X）" % (words[0] if words else 0, vtor))

    print("\n=== 6. 收尾：探针还活着吗 ===")
    r = raw(dev, [ID_DAP_TRANSFER, 0x00, 0x01, 0x02], "DP read IDCODE")
    check(r is not None and acked(r[1]), "IDCODE 仍可读")

    print("\n%s" % ("全部通过 ✓" if not FAILS else "失败 %d 项：%s" % (len(FAILS), FAILS)))
    return 0 if not FAILS else 1


if __name__ == "__main__":
    watchdog(60)
    sys.exit(main())
