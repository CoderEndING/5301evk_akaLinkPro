/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 akaInstruments */

#include "riscv_jtag.h"

/* DAP_config.h must come first: DAP.h uses its pin macros (NOP, PIN_DELAY_*,
 * DAP_JTAG/SWD feature switches). */
#include "DAP_config.h"
#include "DAP.h"
#include "hpm_common.h"

/* ---------------------------------------------------------------- DMI ---- */

#define DMI_OP_NOP   0U
#define DMI_OP_READ  1U
#define DMI_OP_WRITE 2U

#define DMI_OP_STATUS_SUCCESS  0U
#define DMI_OP_STATUS_BUSY     1U

/* Debug Module registers (RISC-V Debug Specification 1.0) */
#define DM_DATA0      0x04U
#define DM_DMCONTROL  0x10U
#define DM_DMSTATUS   0x11U
#define DM_ABSTRACTCS 0x16U
#define DM_SBCS       0x38U
#define DM_SBADDRESS0 0x39U
#define DM_SBDATA0    0x3CU

#define DMCONTROL_DMACTIVE (1UL << 0)

#define SBCS_SBACCESS32   (2UL << 17)
#define SBCS_SBAUTOINC    (1UL << 16)
#define SBCS_SBREADONDATA (1UL << 15)
#define SBCS_SBREADONADDR (1UL << 20)
#define SBCS_SBBUSY       (1UL << 21)
#define SBCS_SBBUSYERROR  (1UL << 22)
#define SBCS_SBERROR      (7UL << 12)

/* Instruction registers of the RISC-V DTM */
#define IR_IDCODE 0x01U
#define IR_DTMCS  0x10U
#define IR_DMI    0x11U

/* TAP / DTM status */
#define DTMCS_VERSION_MASK (0xFUL << 0)
#define DMSTATUS_ANYHALTED (1UL << 9)

/* ------------------------------------------------------------- state ---- */

static uint32_t s_open;
static uint32_t s_idcode;
static uint32_t s_dtmcs;
static uint32_t s_dmstatus;
static uint32_t s_last_sbcs;
static uint32_t s_last_dmstatus;

/* 单字流水读的"抱住"状态：SBA 已按"不自增 + sbreadonaddr + sbreadondata"配好、
 * sbaddress0 已指向 s_hold_addr、且已经投出去一读（响应在下一拍回来）。
 * 任何重新配置 sbcs 的路径（sba_config）都会把它清掉。 */
static uint32_t s_hold_addr;
static uint8_t s_hold_ok;

/* SBCS 配置缓存 + "上次失败过"标志。
 *
 * 一次 dmi_write / dmi_read 都是 **2 次 DMI 扫描**，而一次 8 字块读总共才 18 次扫描 ——
 * 每次调用都"清错 + 重配"要吃掉 6 次（实测 8 通道 32 B span：45.6 µs 里 1/3 是这个）。
 * 目标在跑时偶发 BUSY 才需要清 sticky 错误，所以只在**上一次操作失败过**时清；
 * 配置没变就不重写。 */
static uint32_t s_sbcs_cfg;
static uint8_t s_sbcs_valid;
static uint8_t s_sba_failed;

/* Run-Test/Idle TCK cycles inserted before every DR scan. This is not optional
 * on the HPM6880's DTM: dtmcs.idle reads 7, and without idle clocks the DTM
 * silently stops accepting requests - the response freezes at op = 3 (which
 * looks exactly like "dmcontrol writes are rejected"). 4 clocks already work;
 * 8 keeps a margin, and the cost is ~17% of one 46-clock DMI access. */
static uint32_t s_delay = 8U;

/* Bring-up diagnostics: the raw 41-bit DR values of the last few DMI scans. */
static uint64_t s_dbg[4];
static uint32_t s_dbg_n;

uint64_t riscv_jtag_dbg(uint32_t idx)
{
    return (idx < 4U) ? s_dbg[idx] : 0U;
}

/* ------------------------------------------------------- JTAG plumbing ---- */

/* Shift `nbits` (1..64) TCK cycles at a constant TMS level. TDI is taken from
 * `tdi` (LSB first); TDO is captured into `*tdo` when `tdo` is non-NULL.
 * `info` encoding (see JTAG_DP_GPIO_ASM_45M.S): [5:0] clocks (0 = 64),
 * [6] TMS, [7] capture TDO. */
static void jtag_seq(uint32_t nbits, uint32_t tms, uint64_t tdi, uint64_t *tdo)
{
    uint8_t td[8] = {0};
    uint8_t rd[8] = {0};
    uint32_t nb = (nbits + 7U) / 8U;
    uint32_t info;

    for (uint32_t i = 0U; i < nb; i++)
    {
        td[i] = (uint8_t)(tdi >> (8U * i));
    }

    info = (nbits & 0x3FU) | (tms ? 0x40U : 0U) | (tdo ? 0x80U : 0U);
    JTAG_Sequence(info, td, tdo ? rd : NULL);

    if (tdo != NULL)
    {
        uint64_t v = 0U;
        for (uint32_t i = 0U; i < nb; i++)
        {
            v |= ((uint64_t)rd[i]) << (8U * i);
        }
        *tdo = v;
    }
}

/* Hold TMS high for 48 TCK: from any state this lands in Test-Logic-Reset,
 * then one TMS-low clock parks the TAP in Run-Test/Idle. */
static void tap_reset(void)
{
    for (uint32_t i = 0U; i < 6U; i++)
    {
        jtag_seq(8U, 1U, 0xFFFFFFFFFFFFFFFFULL, NULL);
    }
    jtag_seq(1U, 0U, 1U, NULL);
}

/* RTI -> Shift-IR, shift `ir` (5 bits), Update-IR, back to RTI. */
static void tap_load_ir(uint32_t ir)
{
    jtag_seq(2U, 1U, 0x3U, NULL);            /* Select-DR-Scan, Select-IR-Scan */
    jtag_seq(2U, 0U, 0x3U, NULL);            /* Capture-IR, Shift-IR          */
    jtag_seq(4U, 0U, ir & 0xFU, NULL);       /* IR[3:0], TMS = 0              */
    jtag_seq(1U, 1U, (ir >> 4) & 0x1U, NULL);/* IR[4] -> Exit1-IR             */
    jtag_seq(1U, 1U, 1U, NULL);              /* Update-IR                     */
    jtag_seq(1U, 0U, 1U, NULL);              /* Run-Test/Idle                 */
}

/* One DR scan of `nbits` (RTI -> Shift-DR -> ... -> Update-DR -> RTI).
 * `s_delay` inserts that many Run-Test/Idle TCK cycles first: dtmcs.idle reads
 * 7 on this DTM, i.e. it asks for idle clocks between scans. */
static uint64_t tap_dr_scan(uint32_t nbits, uint64_t tdi)
{
    uint64_t lo = 0U;
    uint64_t hi = 0U;

    if (s_delay != 0U)
    {
        jtag_seq((s_delay > 64U) ? 64U : s_delay, 0U, 0xFFFFFFFFFFFFFFFFULL, NULL);
    }

    jtag_seq(1U, 1U, 1U, NULL);                          /* Select-DR-Scan */
    jtag_seq(2U, 0U, 0x3U, NULL);                        /* Capture-DR, Shift-DR */

    if (nbits > 1U)
    {
        jtag_seq(nbits - 1U, 0U, tdi & ((1ULL << (nbits - 1U)) - 1ULL), &lo);
    }
    jtag_seq(1U, 1U, (tdi >> (nbits - 1U)) & 0x1U, &hi); /* last bit -> Exit1-DR */

    jtag_seq(1U, 1U, 1U, NULL);                          /* Update-DR */
    jtag_seq(1U, 0U, 1U, NULL);                          /* Run-Test/Idle */

    return (lo & ((nbits > 1U) ? ((1ULL << (nbits - 1U)) - 1ULL) : 0ULL)) |
           ((hi & 0x1U) << (nbits - 1U));
}

/* Fast path: one whole DMI access (idle + TAP navigation + 41-bit shift) in a
 * single assembly routine - see dap/JTAG_DP/JTAG_DP_GPIO_ASM_DMI.S. */
extern uint32_t JTAG_DMI_Scan(uint32_t idle, uint32_t dr_lo, uint32_t dr_mid,
                              uint32_t dr_hi, uint32_t *tdo_hi);

/* Post one DMI request; returns the *response* to the request posted by the
 * previous call (the DMI pipeline is one deep). */
static uint64_t dmi_post(uint32_t op, uint32_t addr, uint32_t data)
{
    uint64_t dr = (uint64_t)(op & 0x3U) |
                  ((uint64_t)data << 2) |
                  ((uint64_t)(addr & 0x7FU) << 34);
    uint32_t tdo_hi = 0U;
    uint32_t tdo_lo = JTAG_DMI_Scan(s_delay,
                                    (uint32_t)(dr & 0xFFFFFFFFULL),
                                    (uint32_t)((dr >> 32) & 0xFFULL),
                                    (uint32_t)((dr >> 40) & 0x1ULL),
                                    &tdo_hi);
    uint64_t resp = (uint64_t)tdo_lo | ((uint64_t)(tdo_hi & 0x1FFU) << 32);

    s_dbg[s_dbg_n & 3U] = resp;
    s_dbg_n++;

    return resp;
}

static uint32_t dmi_resp_op(uint64_t resp)
{
    return (uint32_t)(resp & 0x3U);
}

static uint32_t dmi_resp_data(uint64_t resp)
{
    return (uint32_t)((resp >> 2) & 0xFFFFFFFFULL);
}

/* Synchronous DMI read (2 scans). Retries while the DM reports BUSY. */
static int dmi_read(uint32_t addr, uint32_t *val)
{
    for (uint32_t retry = 0U; retry < 8U; retry++)
    {
        uint64_t resp;

        (void)dmi_post(DMI_OP_READ, addr, 0U);   /* flush any pending response */
        resp = dmi_post(DMI_OP_NOP, 0U, 0U);     /* this is the read response  */

        if (dmi_resp_op(resp) == DMI_OP_STATUS_SUCCESS)
        {
            if (val != NULL)
            {
                *val = dmi_resp_data(resp);
            }
            return 0;
        }
        if (dmi_resp_op(resp) != DMI_OP_STATUS_BUSY)
        {
            return -1;
        }
    }
    return -1;
}

/* Synchronous DMI write (2 scans: issue, then collect the write's status). */
static int dmi_write(uint32_t addr, uint32_t data)
{
    uint64_t resp;

    (void)dmi_post(DMI_OP_WRITE, addr, data);
    resp = dmi_post(DMI_OP_NOP, 0U, 0U);

    return (dmi_resp_op(resp) == DMI_OP_STATUS_SUCCESS) ? 0 : -1;
}

/* --------------------------------------------------------------- SBA ---- */

static uint32_t sba_clear_errors(void)
{
    uint32_t sbcs = 0U;

    if (dmi_read(DM_SBCS, &sbcs) != 0)
    {
        return 0U;
    }
    s_last_sbcs = sbcs;

    if ((sbcs & (SBCS_SBBUSYERROR | SBCS_SBERROR)) != 0U)
    {
        /* sbbusyerror(bit22) / sberror([14:12]) 都是 **写 1 清零**。早先写成
         * `sbcs & ~bits`（写 0）等于什么都没做，于是第一次出错之后 SBA 永久
         * 卡在错误态：RTT 桥表现为"搬了一块就再也搬不动"（rderr 每轮必涨、
         * moves 恒等于 1）。这里保持配置位不变、把错误位写 1。 */
        (void)dmi_write(DM_SBCS, sbcs | SBCS_SBBUSYERROR | SBCS_SBERROR);
    }
    return sbcs;
}

/* Public wrapper: the bridge's error-recovery path needs the same "clear the
 * sticky bits" step the SWD backend gets from swd_clear_errors(). */
uint32_t riscv_jtag_clear_errors(void)
{
    return sba_clear_errors();
}

/* 写 SBCS 并更新缓存（失败就把缓存标脏，下次一定重写）。 */
static int sba_write_cfg(uint32_t sbcs)
{
    if (dmi_write(DM_SBCS, sbcs) != 0)
    {
        s_sbcs_valid = 0U;
        return -1;
    }
    s_sbcs_cfg = sbcs;
    s_sbcs_valid = 1U;
    s_last_sbcs = sbcs;      /* 诊断用：写进去的就是当前值，不必再读回来 */
    return 0;
}

static int sba_config(uint32_t extra)
{
    uint32_t sbcs = SBCS_SBACCESS32 | SBCS_SBAUTOINC | extra;

    /* 重新配置 sbcs 会把"抱住固定地址"的状态一起作废（见 riscv_jtag_hold_prepare）。 */
    s_hold_ok = 0U;

    /* 🚨 只在**上一次操作失败过**时清 sticky 错误（理由见上面缓存那段注释）。
     * 安全性：各失败路径自己会清，块读外面还有 riscv_jtag_read() 的整块重试兜底。 */
    if (s_sba_failed)
    {
        (void)sba_clear_errors();
        s_sba_failed = 0U;
    }

    if (s_sbcs_valid && (s_sbcs_cfg == sbcs))
    {
        return 0;                        /* 已经是这个配置：0 次扫描 */
    }
    return sba_write_cfg(sbcs);
}

/* ------------------------------------------------------------ public ---- */

int riscv_jtag_is_open(void)
{
    return (int)s_open;
}

void riscv_jtag_close(void)
{
    s_open = 0U;
    s_sbcs_valid = 0U;       /* TAP 关掉后 DM 里的配置不再是"已知状态" */
    s_sba_failed = 0U;
    s_hold_ok = 0U;
    DAP_Data.debug_port = DAP_PORT_DISABLED;
    PORT_OFF();
}

uint32_t riscv_jtag_idcode(void)   { return s_idcode; }
uint32_t riscv_jtag_dtmcs(void)    { return s_dtmcs; }
uint32_t riscv_jtag_dmstatus(void) { return s_dmstatus; }
uint32_t riscv_jtag_last_sbcs(void)     { return s_last_sbcs; }
uint32_t riscv_jtag_last_dmstatus(void) { return s_last_dmstatus; }

void riscv_jtag_set_delay(uint32_t delay)
{
    s_delay = delay;
    DAP_Data.clock_delay = (uint8_t)delay;
}

uint32_t riscv_jtag_get_delay(void)
{
    return s_delay;
}

int riscv_jtag_open(void)
{
    uint64_t dr;

    s_open = 0U;
    s_idcode = 0U;
    s_dtmcs = 0U;
    s_dmstatus = 0U;
    s_sbcs_valid = 0U;       /* TAP 复位会把 DM 的寄存器打回默认，缓存必须作废 */
    s_sba_failed = 0U;
    s_hold_ok = 0U;

    DAP_Data.debug_port = DAP_PORT_JTAG;
    PORT_JTAG_SETUP();

    tap_reset();

    /* TAP IDCODE (IR = 0x01, 32-bit DR) - a cheap "is anything there" check. */
    tap_load_ir(IR_IDCODE);
    dr = tap_dr_scan(32U, 0xFFFFFFFFULL);
    s_idcode = (uint32_t)dr;
    if ((s_idcode == 0U) || (s_idcode == 0xFFFFFFFFU))
    {
        return -1;
    }

    /* DTM control/status: version 1 (or 0 on early revisions). */
    tap_load_ir(IR_DTMCS);
    dr = tap_dr_scan(32U, 0xFFFFFFFFULL);
    s_dtmcs = (uint32_t)dr;

    /* The DTM stops answering the DMI after the dtmcs/idcode scans unless the
     * TAP is taken through Test-Logic-Reset again - verified on the probe by
     * comparing against a fresh reset (see riscv_jtag_probe). */
    tap_reset();
    tap_load_ir(IR_DMI);

    /* Park the DMI: drain the pipeline, then activate the debug module.
     * dmcontrol.dmactive must be set before the DM answers anything else -
     * without it a DMI request just stays busy (RISC-V Debug Spec 1.0, 3.2). */
    (void)dmi_post(DMI_OP_NOP, 0U, 0U);
    (void)dmi_post(DMI_OP_NOP, 0U, 0U);
    if (dmi_write(DM_DMCONTROL, DMCONTROL_DMACTIVE) != 0)
    {
        return -3;
    }

    if (dmi_read(DM_DMSTATUS, &s_dmstatus) != 0)
    {
        return -2;
    }
    s_last_dmstatus = s_dmstatus;

    /* Clear sticky SBA errors from a previous session. */
    s_last_sbcs = sba_clear_errors();

    s_open = 1U;
    return 0;
}

int riscv_jtag_read_word(uint32_t addr, uint32_t *val)
{
    if (s_open == 0U)
    {
        return -1;
    }
    if (sba_config(SBCS_SBREADONADDR) != 0)
    {
        return -2;
    }
    if (dmi_write(DM_SBADDRESS0, addr) != 0)
    {
        return -3;
    }
    if (dmi_read(DM_SBDATA0, val) != 0)
    {
        return -4;
    }
    return 0;
}

/* ---- 单字流水读（J-Scope 单变量快路径）------------------------------------
 *
 * 与块读共用同一套 DMI 流水（一次访问 = 一条 41 位 DR 扫描，响应在**下一拍**回来），
 * 区别只在 sbcs：**关掉自增**，于是每拍读的都是同一个地址 —— 正好是"盯着一个变量
 * 看它随时间变"要的语义。配置一次（prepare）之后，每拍只剩一次 dmi_post：
 *     sbcs       = sbaccess32 | sbreadonaddr | sbreadondata    （不自增）
 *     sbaddress0 = addr      -> 写地址即启动第一次读
 *     读一次 sbdata0         -> 点火：收上一拍的值，同时启动下一拍
 * 之后每次 hold_read 就是"收上一次 + 投下一次"，语义与 swd_read_word_pipe() 一致。 */
int riscv_jtag_hold_prepare(uint32_t addr)
{
    addr &= ~0x3U;

    if (s_open == 0U)
    {
        return -1;
    }
    if (s_hold_ok && (s_hold_addr == addr))
    {
        return 0;                     /* 已经抱着这个地址：空操作（每拍调也没关系） */
    }

    s_hold_ok = 0U;
    if (s_sba_failed)
    {
        (void)sba_clear_errors();
        s_sba_failed = 0U;
    }
    if (sba_write_cfg(SBCS_SBACCESS32 | SBCS_SBREADONADDR | SBCS_SBREADONDATA) != 0)
    {
        return -2;
    }
    if (dmi_write(DM_SBADDRESS0, addr) != 0)
    {
        return -3;
    }
    (void)dmi_post(DMI_OP_READ, DM_SBDATA0, 0U);   /* 点火：它的响应由第一次 hold_read 收 */

    s_hold_addr = addr;
    s_hold_ok = 1U;
    return 0;
}

int riscv_jtag_hold_read(uint32_t *val)
{
    uint64_t resp;

    if (!s_hold_ok)
    {
        return -1;
    }

    resp = dmi_post(DMI_OP_READ, DM_SBDATA0, 0U);

    /* 目标全速运行时 DM 偶发 BUSY：同一个地址重读是幂等的，重发几次再判死
     * （与块读同一策略，见 riscv_jtag_read_once）。 */
    for (uint32_t r = 0U; (r < 4U) && (dmi_resp_op(resp) != DMI_OP_STATUS_SUCCESS); r++)
    {
        resp = dmi_post(DMI_OP_READ, DM_SBDATA0, 0U);
    }
    if (dmi_resp_op(resp) != DMI_OP_STATUS_SUCCESS)
    {
        s_last_sbcs = sba_clear_errors();
        s_sba_failed = 1U;
        s_hold_ok = 0U;              /* 出错后"抱住"不可信：下一次 prepare 重新配 */
        return -1;
    }

    if (val != NULL)
    {
        *val = dmi_resp_data(resp);
    }
    return 0;
}

int riscv_jtag_write_word(uint32_t addr, uint32_t val)
{
    if (s_open == 0U)
    {
        return -1;
    }
    if (sba_config(0U) != 0)
    {
        return -2;
    }
    if (dmi_write(DM_SBADDRESS0, addr) != 0)
    {
        return -3;
    }
    if (dmi_write(DM_SBDATA0, val) != 0)
    {
        return -4;
    }
    return 0;
}

/* Bulk read: one posted DMI read per word, no per-word round trip.
 *
 * sbreadondata makes every read of sbdata0 start the next system bus read, so
 * the loop below keeps exactly one read in flight and the DMI pipeline (one
 * deep) supplies the previous word in the same scan. */
int riscv_jtag_read_once(uint32_t addr, uint8_t *dst, uint32_t len)
{
    uint32_t first;
    uint32_t words;
    uint32_t head;
    uint32_t *out = (uint32_t *)(void *)dst;
    uint8_t tail[4];
    uint32_t i;

    if ((s_open == 0U) || (len == 0U))
    {
        return -1;
    }

    first = addr & ~0x3U;
    head = addr - first;
    words = (head + len + 3U) / 4U;

    if (sba_config(SBCS_SBREADONADDR | SBCS_SBREADONDATA) != 0)
    {
        return -2;
    }
    if (dmi_write(DM_SBADDRESS0, first) != 0)
    {
        return -3;
    }

    /* Kick the pipeline: the response to this request is the first word. */
    (void)dmi_post(DMI_OP_READ, DM_SBDATA0, 0U);

    if (head == 0U)
    {
        for (i = 0U; i < words; i++)
        {
            uint64_t resp = dmi_post(DMI_OP_READ, DM_SBDATA0, 0U);

            if (dmi_resp_op(resp) != DMI_OP_STATUS_SUCCESS)
            {
                /* 目标在跑时 DM 偶发 BUSY：把这一拍重发几次再判死，否则一个
                 * 1 KB 块会因为一个字整块失败（实测每块必中，搬运几乎停摆）。 */
                uint32_t ok = 0U;

                for (uint32_t r = 0U; r < 4U; r++)
                {
                    resp = dmi_post(DMI_OP_READ, DM_SBDATA0, 0U);
                    if (dmi_resp_op(resp) == DMI_OP_STATUS_SUCCESS)
                    {
                        ok = 1U;
                        break;
                    }
                }
                if (ok == 0U)
                {
                    s_last_sbcs = sba_clear_errors();
                    s_sba_failed = 1U;
                    return -4;
                }
            }
            out[i] = dmi_resp_data(resp);
        }
    }
    else
    {
        uint32_t w = 0U;

        for (i = 0U; i < words; i++)
        {
            uint64_t resp = dmi_post(DMI_OP_READ, DM_SBDATA0, 0U);

            if (dmi_resp_op(resp) != DMI_OP_STATUS_SUCCESS)
            {
                s_last_sbcs = sba_clear_errors();
                s_sba_failed = 1U;
                return -4;
            }
            w = dmi_resp_data(resp);
            if (i == 0U)
            {
                uint8_t *p = (uint8_t *)&w;
                uint32_t n = 4U - head;

                if (n > len)
                {
                    n = len;
                }
                for (uint32_t k = 0U; k < n; k++)
                {
                    dst[k] = p[head + k];
                }
            }
            else if (i == (words - 1U))
            {
                uint8_t *p = (uint8_t *)&w;
                uint32_t done = 4U - head + (i - 1U) * 4U;
                uint32_t n = len - done;

                if (n > 4U)
                {
                    n = 4U;
                }
                for (uint32_t k = 0U; k < n; k++)
                {
                    dst[done + k] = p[k];
                }
            }
            else
            {
                uint32_t done = 4U - head + (i - 1U) * 4U;
                for (uint32_t k = 0U; k < 4U; k++)
                {
                    dst[done + k] = ((uint8_t *)&w)[k];
                }
            }
        }
    }

    /* Drain the extra request that is still in flight. */
    (void)dmi_post(DMI_OP_NOP, 0U, 0U);
    (void)tail;

    /* 🚨 成功路径**不**再读一遍 SBCS 清错（那是 2 次扫描的纯开销）：出错时上面各
     * 分支已经清过，下一次 sba_config() 也会因为 s_sba_failed 再清一次。 */
    return 0;
}

/* Bulk write: one posted write per word (no response needed). */
int riscv_jtag_write(uint32_t addr, const uint8_t *src, uint32_t len)
{
    uint32_t first;
    uint32_t words;
    uint32_t head;

    if ((s_open == 0U) || (len == 0U))
    {
        return -1;
    }

    first = addr & ~0x3U;
    head = addr - first;
    words = (head + len + 3U) / 4U;

    if (sba_config(0U) != 0)
    {
        return -2;
    }
    if (dmi_write(DM_SBADDRESS0, first) != 0)
    {
        return -3;
    }

    if ((head == 0U) && (len == (words * 4U)))
    {
        const uint32_t *in = (const uint32_t *)(const void *)src;

        for (uint32_t i = 0U; i < words; i++)
        {
            (void)dmi_post(DMI_OP_WRITE, DM_SBDATA0, in[i]);
        }
    }
    else
    {
        uint32_t done = 0U;

        for (uint32_t i = 0U; i < words; i++)
        {
            uint32_t w = 0U;
            uint32_t n = 4U - head;
            uint32_t off = head;

            if (i == 0U)
            {
                if (n > len)
                {
                    n = len;
                }
                for (uint32_t k = 0U; k < n; k++)
                {
                    ((uint8_t *)&w)[off + k] = src[k];
                }
                done = n;
            }
            else
            {
                (void)off;
                n = len - done;
                if (n > 4U)
                {
                    n = 4U;
                }
                for (uint32_t k = 0U; k < n; k++)
                {
                    ((uint8_t *)&w)[k] = src[done + k];
                }
                done += n;
            }

            (void)dmi_post(DMI_OP_WRITE, DM_SBDATA0, w);

            if (i == 0U)
            {
                head = 0U; /* subsequent words are aligned to the target */
            }
        }
    }

    (void)dmi_post(DMI_OP_NOP, 0U, 0U); /* drain the stale response */
    return 0;
}

/* Bring-up helper: reset the TAP, load IR = DMI, then run four requests and
 * hand back the raw 41-bit responses (each one is the response to the request
 * issued in the *previous* scan):
 *   [0] <- stale (pre-reset)      request: WRITE dmcontrol = 1
 *   [1] <- result of the WRITE    request: NOP
 *   [2] <- result of the NOP      request: READ dmstatus
 *   [3] <- value of dmstatus      request: NOP
 * Shows whether the DMI answers, how it encodes op for write vs read, and what
 * dmstatus actually holds. */
void riscv_jtag_probe(uint32_t n, uint64_t *out)
{
    if (n > 8U)
    {
        n = 8U;
    }

    DAP_Data.debug_port = DAP_PORT_JTAG;
    PORT_JTAG_SETUP();
    tap_reset();
    tap_load_ir(IR_DMI);

    out[0] = dmi_post(DMI_OP_WRITE, DM_DMCONTROL, DMCONTROL_DMACTIVE);
    out[1] = dmi_post(DMI_OP_NOP, 0U, 0U);
    out[2] = dmi_post(DMI_OP_READ, DM_DMSTATUS, 0U);
    out[3] = dmi_post(DMI_OP_NOP, 0U, 0U);

    /* Leave the port idle again: on the evklite the JTAG pins are the chip's
     * own debug pins, so they must not stay armed when nothing is running. */
    PORT_OFF();
}

/* 目标 CPU 在跑的时候，SBA 偶发一次非 0 响应（sbbusy/sberror）就把整个 1 KB
 * 块判死，实测 RTT 桥下每块几乎必中一次 -> 搬运几乎停摆。这里按块重试：
 * 清掉 sticky 错误再重来，通常第二次就干净。 */
int riscv_jtag_read(uint32_t addr, uint8_t *dst, uint32_t len)
{
    int rc = -4;

    for (uint32_t attempt = 0U; attempt < 3U; attempt++)
    {
        rc = riscv_jtag_read_once(addr, dst, len);
        if (rc == 0)
        {
            break;
        }
        s_last_sbcs = sba_clear_errors();
    }
    return rc;
}
