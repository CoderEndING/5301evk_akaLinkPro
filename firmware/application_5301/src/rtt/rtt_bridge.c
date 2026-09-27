/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 akaInstruments */

/* Probe-side SEGGER RTT bridge.
 *
 * Everything the host would normally do over CMSIS-DAP (read the control
 * block, drain the ring, write RdOff back) is done here, in-process, by
 * building the same DAP requests a host would send and feeding them straight
 * into DAP_ExecuteCommand(). That reuses the pin muxing, SWD clock, ACK
 * handling and WAIT retries of the DAP engine - only the USB round trips
 * disappear. Drained bytes land in the CDC ringbuffer, which the existing
 * CDC IN path already forwards to the host.
 *
 * Arbitration: the SWD bus belongs to the DAP. A poll only runs when no DAP
 * command has been executed for RTT_BRIDGE_DAP_IDLE_MS, and each poll is
 * bounded (a few hundred bytes), so a debug session sees at most ~1 ms of
 * extra latency and never a corrupted transfer.
 */

#include <string.h>

#include "board.h"
#include "hpm_common.h"
/* usb_composite.h pulls in DAP_config.h, which is what defines
 * __STATIC_FORCEINLINE and the DAP_SWD/DAP_JTAG switches DAP.h relies on. */
#include "usb_composite.h"
#include "DAP.h"
#include "cdc_interface.h"
#include "rtt_bridge.h"
#include "swd_host.h"   /* ARM DAPLink 官方 SWD 访问层（见 src/swd_host/） */

/* Debug port / AHB-AP 的寄存器与传输编码由官方 swd_host.c / debug_cm.h 负责，
 * 这里只留桥自己的配置常量。 */

/* SWD clock used by the bridge: fast enough to move a 12 KB ring, slow enough
 * to stay reliable while the target runs (45/60 MHz start failing there). */
#define RTT_SWD_CLOCK_HZ 36000000UL

/* SEGGER RTT layout (SEGGER_RTT.h): CB header 24 bytes, then per up-buffer
 * {name, pBuffer, SizeOfBuffer, WrOff, RdOff, Flags} = 24 bytes. */
#define RTT_CB_UP_OFFSET 24U
#define RTT_UP_DESC_SIZE 24U
#define RTT_UP_WROFF_OFF 12U
#define RTT_UP_RDOFF_OFF 16U

/* 每次轮询最多搬运的字节数。 */
#define RTT_MAX_DRAIN 2048U

/* MCHTMR runs at 24 MHz (osc24m). */
#define RTT_MCHTMR_HZ 24000000UL
#define RTT_IDLE_TICKS (RTT_BRIDGE_DAP_IDLE_MS * (RTT_MCHTMR_HZ / 1000U))
#define RTT_ERROR_BACKOFF_MS 100U
#define RTT_RESCAN_AFTER 3U

typedef struct
{
    uint32_t name;
    uint32_t buf;
    uint32_t size;
    uint32_t wr;
    uint32_t rd;
    uint32_t flags;
} rtt_up_desc_t;

static volatile uint8_t s_running;
static uint8_t s_channel;
static uint32_t s_cb_addr;
static uint32_t s_search_addr;
static uint32_t s_search_size;
static uint32_t s_up_addr;
static uint32_t s_last_dap;
static uint32_t s_backoff_until;
/* 上次没写成功、待幂等补写的 RdOff（0 值有效，用 valid 标志区分） */
static uint32_t s_rd_pending;
static uint8_t s_rd_pending_valid;

static uint32_t s_drained;
static uint32_t s_polls;
static uint32_t s_drains;
static uint32_t s_read_err;
static uint32_t s_write_err;
static uint32_t s_rescans;
static uint32_t s_gate_hits;
static uint32_t s_zips;
static uint32_t s_last_poll_bytes;
/* last DAP response, for diagnosing an unresponsive target */
static uint32_t s_last_cmd;
static uint32_t s_last_rsp;

static uint8_t s_req[16];
static uint8_t s_resp[8U];
static uint8_t s_stage[RTT_MAX_DRAIN];

static uint32_t mchtmr_now(void)
{
    return *(volatile uint32_t *)(HPM_MCHTMR_BASE + 0x00);
}

/* ------------------------------------------------------------------ */
/* SWD plumbing —— 直接使用 ARM DAPLink 官方 swd_host.c                */
/*                                                                     */
/* 桥的 SWD 访问全部走官方 API：                                       */
/*   swd_init()        —— DAP_Setup + PORT_SWD_SETUP + debug_port=SWD   */
/*   swd_init_debug()  —— JTAG2SWD 切换 + 清错 + DP 上电 + SELECT=0     */
/*   swd_read_memory() —— 块读（内部即 swd_read_block/AP 自动递增）     */
/*   swd_write_word()  —— 写 32 位字                                    */
/* 底层单次传输 SWD_Transfer() 由 swd_host_port.c 分派到               */
/* SWD_Read()/SWD_Write()（SW_DP.c，与 DAP 主机通路同一套 bit-bang）。  */
/* ------------------------------------------------------------------ */

/* The only DAP command the bridge still issues: SWJ_Clock, to load the fast
 * bit-bang blob and set DAP_Data.clock_delay. */
static void dap_cmd(const uint8_t *req, uint8_t *resp)
{
    (void)DAP_ExecuteCommand(req, resp);
    s_last_cmd = req[0];
    s_last_rsp = (uint32_t)resp[0] | ((uint32_t)resp[1] << 8) |
                 ((uint32_t)resp[2] << 16) | ((uint32_t)resp[3] << 24);
}

/* 单次块读的字节数：官方 swd_read_memory() 内部按 1 KB 页切分，这里再限到
 * 512 B（=128 字）—— 实测这是目标运行中长块读不会 WAIT/FAULT 的可靠尺寸。 */
#define RTT_SWD_CHUNK 512U

/* Load the fast bit-bang blob and set the SWD timing. Uses the DAP command
 * only to reach Set_Clock_Delay(); it performs no SWD traffic. */
static int rtt_swd_set_clock(uint32_t hz)
{
    s_req[0] = ID_DAP_SWJ_Clock;
    s_req[1] = (uint8_t)(hz >> 0);
    s_req[2] = (uint8_t)(hz >> 8);
    s_req[3] = (uint8_t)(hz >> 16);
    s_req[4] = (uint8_t)(hz >> 24);
    dap_cmd(s_req, s_resp);
    return (s_resp[2] == DAP_OK) ? 0 : -1;
}

/* Debug-port bring-up：官方 swd_host 的 swd_init() + swd_init_debug()。
 * 顺序不能反：swd_init() 里的 DAP_Setup() 会把时钟重置成默认档并加载 Slow
 * blob，所以 SWJ_Clock 必须在其之后、swd_init_debug() 之前调用。
 * swd_init_debug() 内部即 JTAG2SWD（52 高 + 0xE79E + 57 高 + 读 IDCODE）、
 * 清 sticky 错误、SELECT=0、DP 上电并等待 CSYSPWRUPACK|CDBGPWRUPACK、
 * 再设 TRNNORMAL|MASKLANE 并回到 SELECT=0；失败时它还带 4 次重试
 * （含一次 nRESET 硬复位）。 */
static int rtt_swd_init(void)
{
    swd_init();     /* DAP_Setup + PORT_SWD_SETUP + DAP_Data.debug_port = SWD */

    /* 先在默认（低速）档完成 JTAG2SWD 握手与 DP 上电 —— 和真实主机一样，
     * 连接阶段不用高速档。注意 swd_init_debug() 内部还会再调一次 swd_init()，
     * 其 DAP_Setup() 会把时钟档重置回默认，所以提速只能放在它之后。 */
    if (swd_init_debug() == 0U)
    {
        return -2;
    }

    if (rtt_swd_set_clock(RTT_SWD_CLOCK_HZ) != 0)
    {
        return -1;
    }

    return 0;
}

/* Read `len` bytes from an arbitrary (possibly unaligned) address.
 * 官方 swd_read_memory() 自己会处理未对齐的头/尾字节，这里只负责把它切成
 * 512 B 的块（目标运行中长块读会 WAIT/FAULT）。 */
static int rtt_read_bytes(uint32_t addr, uint8_t *dst, uint32_t len)
{
    uint32_t done = 0U;

    while (done < len)
    {
        uint32_t n = len - done;
        if (n > RTT_SWD_CHUNK)
        {
            n = RTT_SWD_CHUNK;
        }
        if (swd_read_memory(addr + done, &dst[done], n) == 0U)
        {
            return -1;
        }
        done += n;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Control block discovery                                             */
/* ------------------------------------------------------------------ */

static int rtt_find_cb(void)
{
    uint8_t sig[RTT_BRIDGE_SIG_LEN];

    s_cb_addr = 0U;
    for (uint32_t addr = s_search_addr; addr + RTT_BRIDGE_SIG_LEN <= s_search_addr + s_search_size; addr += 4U)
    {
        if (rtt_read_bytes(addr, sig, RTT_BRIDGE_SIG_LEN) != 0)
        {
            s_read_err++;
            continue;
        }
        if (memcmp(sig, RTT_BRIDGE_SIGNATURE, RTT_BRIDGE_SIG_LEN) == 0)
        {
            s_cb_addr = addr;
            s_up_addr = addr + RTT_CB_UP_OFFSET + (uint32_t)s_channel * RTT_UP_DESC_SIZE;
            return 0;
        }
    }
    return -1;
}

/* ------------------------------------------------------------------ */
/* Draining                                                            */
/* ------------------------------------------------------------------ */

static int rtt_poll_once(uint32_t *moved)
{
    rtt_up_desc_t up;
    uint32_t avail, target, first, free_space;

    *moved = 0U;

    /* RdOff 写失败的幂等重试：RdOff 是绝对值，把同一个值再写一遍永远安全，
     * 所以先补写上次没写成功的那次，再继续搬新数据。不这样做的话，
     * swd_write_data() 里收尾的 dummy RDBUFF 读一旦失败（此时写其实已经落到
     * 目标上了）就会被误判为"没写成功"，两边对不齐 —— 要么丢一段、要么重一段。 */
    if (s_rd_pending_valid)
    {
        if (swd_write_word(s_up_addr + RTT_UP_RDOFF_OFF, s_rd_pending) == 0U)
        {
            s_write_err++;
            return -1; /* 下轮再补 */
        }
        s_rd_pending_valid = 0U;
    }

    if (rtt_read_bytes(s_up_addr, (uint8_t *)&up, RTT_UP_DESC_SIZE) != 0)
    {
        s_read_err++;
        return -1;
    }
    if ((up.size == 0U) || (up.rd >= up.size) || (up.wr > up.size))
    {
        /* Control block looks bogus: re-scan. */
        s_read_err++;
        return -1;
    }

    avail = (up.wr >= up.rd) ? (up.wr - up.rd) : (up.size - up.rd);
    if (avail == 0U)
    {
        s_zips++;
        return 0;
    }

    free_space = chry_ringbuffer_get_free(&g_uartrx);
    target = avail;
    if (target > RTT_MAX_DRAIN)
    {
        target = RTT_MAX_DRAIN;
    }
    if (target > free_space)
    {
        target = free_space;
    }
    if (target == 0U)
    {
        return 0; /* the host is not draining fast enough */
    }

    first = up.size - up.rd;
    if (first > target)
    {
        first = target;
    }
    if (rtt_read_bytes((uint32_t)up.buf + up.rd, s_stage, first) != 0)
    {
        s_read_err++;
        return -1;
    }
    if (target > first)
    {
        if (rtt_read_bytes((uint32_t)up.buf, &s_stage[first], target - first) != 0)
        {
            s_read_err++;
            return -1;
        }
    }

    /* 先交付到 CDC 环，再推进目标侧 RdOff：这样即使 RdOff 写失败，字节也已经
     * 送到了主机，绝不会丢；RdOff 由上面的幂等重试补齐，也不会重。 */
    chry_ringbuffer_write(&g_uartrx, s_stage, target);

    s_drained += target;
    s_drains++;
    *moved = target;

    uint32_t new_rd = (up.rd + target) % up.size;
    if (swd_write_word(s_up_addr + RTT_UP_RDOFF_OFF, new_rd) == 0U)
    {
        s_write_err++;
        s_rd_pending = new_rd;
        s_rd_pending_valid = 1U;
        return -1;
    }

    return 0;
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

int rtt_bridge_start(uint32_t addr, uint32_t size, uint8_t channel)
{
    rtt_bridge_stop();

    s_channel = channel;
    s_search_addr = (addr != 0U) ? addr : RTT_BRIDGE_DEFAULT_ADDR;
    s_search_size = (size != 0U) ? size : RTT_BRIDGE_DEFAULT_SIZE;

    /* 每次启动清零计数器：一次实验的读数只反映这一次。 */
    s_drained = 0U;
    s_polls = 0U;
    s_drains = 0U;
    s_read_err = 0U;
    s_write_err = 0U;
    s_rescans = 0U;
    s_gate_hits = 0U;
    s_zips = 0U;
    s_last_poll_bytes = 0U;
    s_rd_pending_valid = 0U;

    int rc = rtt_swd_init();
    if (rc != 0)
    {
        return rc;
    }
    if (rtt_find_cb() != 0)
    {
        return -3;
    }

    /* The UART must not feed the same CDC ringbuffer while RTT does. */
    uartx_set_cdc_source(1);

    s_last_dap = mchtmr_now();
    s_backoff_until = 0U;
    s_running = 1U;
    return 0;
}

void rtt_bridge_stop(void)
{
    if (s_running)
    {
        uartx_set_cdc_source(0);
    }
    s_running = 0U;
    s_cb_addr = 0U;
    s_up_addr = 0U;
    s_rd_pending_valid = 0U;
}

int rtt_bridge_is_running(void)
{
    return s_running ? 1 : 0;
}

void rtt_bridge_note_dap_activity(void)
{
    s_last_dap = mchtmr_now();
}

/* Bring-up debugging: keep the last host DAP request/response so the exact
 * bytes OpenOCD sends can be compared with the ones the bridge builds.
 * (32 bytes request + 32 bytes response, read back with the HID PEEK action.) */
volatile uint8_t g_dap_trace[64];

void rtt_bridge_trace_dap(const uint8_t *req, const uint8_t *resp)
{
    for (uint32_t i = 0U; i < 32U; i++)
    {
        g_dap_trace[i] = req[i];
        g_dap_trace[32U + i] = resp[i];
    }
}

/* ------------------------------------------------------------------ */
/* Deferred execution (never touch SWD from the USB interrupt)         */
/* ------------------------------------------------------------------ */

static volatile uint8_t s_start_pending;
static volatile uint8_t s_raw_pending;
static uint32_t s_req_addr, s_req_size;
static uint8_t s_req_channel;
static uint8_t s_raw_req[24];
static uint32_t s_raw_req_len;
static uint8_t s_raw_rsp[24];
static volatile uint32_t s_raw_rsp_len;
static volatile int8_t s_start_rc = -100;

void rtt_bridge_request_start(uint32_t addr, uint32_t size, uint8_t channel)
{
    s_req_addr = addr;
    s_req_size = size;
    s_req_channel = channel;
    s_start_rc = -100;
    s_start_pending = 1U;
}

void rtt_bridge_request_raw(const uint8_t *req, uint32_t len)
{
    if (len > sizeof(s_raw_req))
    {
        len = sizeof(s_raw_req);
    }
    for (uint32_t i = 0U; i < len; i++)
    {
        s_raw_req[i] = req[i];
    }
    s_raw_req_len = len;
    s_raw_rsp_len = 0U;
    s_raw_pending = 1U;
}

uint32_t rtt_bridge_raw_result(uint8_t *out, uint32_t max)
{
    uint32_t n = s_raw_rsp_len;
    if (n > max)
    {
        n = max;
    }
    for (uint32_t i = 0U; i < n; i++)
    {
        out[i] = s_raw_rsp[i];
    }
    return n;
}

/* Runs from the main loop: performs any queued SWD work. */
static void rtt_bridge_service_requests(void)
{
    if (s_raw_pending)
    {
        s_raw_pending = 0U;
        for (uint32_t i = 0U; i < sizeof(s_raw_rsp); i++)
        {
            s_raw_rsp[i] = 0U;
        }
        (void)DAP_ExecuteCommand(s_raw_req, s_raw_rsp);
        s_raw_rsp_len = sizeof(s_raw_rsp);
    }
    if (s_start_pending)
    {
        s_start_pending = 0U;
        s_start_rc = (int8_t)rtt_bridge_start(s_req_addr, s_req_size, s_req_channel);
    }
}

int rtt_bridge_start_result(void)
{
    return (int)s_start_rc;
}

void rtt_bridge_poll(void)
{
    uint32_t moved = 0U;
    static uint8_t err_run;

    /* Queued SWD work (bridge start / raw passthrough) always runs here, in
     * the main loop: the bit-bang engine is timing critical and must not be
     * driven from the USB interrupt. */
    rtt_bridge_service_requests();

    if (!s_running)
    {
        return;
    }

    uint32_t now = mchtmr_now();
    if ((uint32_t)(now - s_last_dap) < RTT_IDLE_TICKS)
    {
        s_gate_hits++;
        return; /* the host is debugging: leave the SWD bus alone */
    }
    if ((s_backoff_until != 0U) && ((int32_t)(now - s_backoff_until) < 0))
    {
        return;
    }

    s_polls++;
    if (rtt_poll_once(&moved) != 0)
    {
        s_last_poll_bytes = 0U;
        if (++err_run >= RTT_RESCAN_AFTER)
        {
            err_run = 0U;
            s_rescans++;
            s_cb_addr = 0U;
            if (rtt_find_cb() != 0)
            {
                /* Target gone or RTT not initialised: back off, keep trying. */
                s_backoff_until = now + (RTT_ERROR_BACKOFF_MS * (RTT_MCHTMR_HZ / 1000U));
                return;
            }
        }
        s_backoff_until = now + (RTT_ERROR_BACKOFF_MS * (RTT_MCHTMR_HZ / 1000U));
        return;
    }
    err_run = 0U;
    s_last_poll_bytes = moved;
}

uint32_t rtt_bridge_status(uint32_t *out, uint32_t words)
{
    uint32_t w[10];

    w[0] = (uint32_t)s_running | ((uint32_t)s_channel << 8) |
           ((DAP_Data.debug_port == DAP_PORT_SWD) ? (1UL << 16) : 0UL) |
           ((uint32_t)DAP_Data.clock_delay << 24);
    w[1] = s_cb_addr;
    w[2] = s_up_addr;
    w[3] = s_drained;
    w[4] = s_polls | (s_drains << 16);
    w[5] = s_read_err | (s_write_err << 16);
    w[6] = s_last_poll_bytes | (s_zips << 16);
    w[7] = s_gate_hits | (s_rescans << 16);
    w[8] = s_last_cmd;
    w[9] = s_last_rsp; /* [0]=id [1]=count_lo [2]=count_hi/port [3]=response value */

    for (uint32_t i = 0; i < words && i < 10U; i++)
    {
        out[i] = w[i];
    }
    return (words < 10U) ? words : 10U;
}
