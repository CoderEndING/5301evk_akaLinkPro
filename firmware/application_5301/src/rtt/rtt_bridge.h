/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 akaInstruments */

#ifndef __RTT_BRIDGE_H__
#define __RTT_BRIDGE_H__

#include <stdint.h>

/* Probe-side SEGGER RTT bridge.
 *
 * Letting the host poll the target's RTT control block over CMSIS-DAP costs
 * three host<->probe round trips per drain, which caps the stream at ~1.1 MB/s
 * on this probe. Here the probe polls the control block itself through the
 * same SWD engine and pushes the drained bytes into the CDC ringbuffer, so the
 * host only has to read a COM port - no round trips at all.
 *
 * The SWD bus is shared with the DAP command engine: polling only starts when
 * no DAP command has been executed for RTT_BRIDGE_DAP_IDLE_MS, so an active
 * debug session always keeps priority.
 */

#define RTT_BRIDGE_DEFAULT_ADDR 0x20000000UL /* where to search for the CB */
#define RTT_BRIDGE_DEFAULT_SIZE 0x00010000UL
#define RTT_BRIDGE_SIGNATURE    "SEGGER RTT"
#define RTT_BRIDGE_SIG_LEN      10U
#define RTT_BRIDGE_DAP_IDLE_MS  20U

/* Start bridging: search [addr, addr+size) for the control block, drain
 * up-buffer `channel` into the CDC. Returns 0 on success, negative on error. */
int rtt_bridge_start(uint32_t addr, uint32_t size, uint8_t channel);

/* Same, but only arms a request: the SWD access happens from the main loop
 * (rtt_bridge_poll), never from the USB interrupt context. Call this from
 * command handlers. */
void rtt_bridge_request_start(uint32_t addr, uint32_t size, uint8_t channel);
int  rtt_bridge_start_result(void);

/* Runtime tuning (HID CMD_RTT action 7). chunk_bytes is the block size of one
 * swd_read_memory() call (clamped to [64, 4096]); swd_clock_hz = 0 keeps the
 * current setting. `discard` = 1 drops the drained bytes instead of pushing
 * them into the CDC ring, which isolates the raw SWD drain rate from the USB
 * path. delay_override != 0xFF forces DAP_Data.clock_delay (the one timing
 * knob the bit-bang blobs expose) instead of the value Set_Clock_Delay()
 * picked for the requested clock. Takes effect immediately. */
void rtt_bridge_configure(uint32_t swd_clock_hz, uint32_t chunk_bytes, uint8_t discard,
                          uint8_t delay_override);

/* Pure SWD read benchmark (HID CMD_RTT actions 8/9): read `bytes` from `addr`
 * of the TARGET `iters` times through the same swd_host path the bridge uses,
 * measuring MCHTMR ticks. Deferred to the main loop like every other SWD op. */
void rtt_bridge_request_bench(uint32_t addr, uint32_t bytes, uint32_t iters);
/* Returns 1 once a result is available, 0 while pending. */
int  rtt_bridge_bench_result(uint32_t *bytes, uint32_t *ticks, int32_t *err);

/* Bring-up debugging: record the last host DAP request/response pair. */
extern volatile uint8_t g_dap_trace[64];
void rtt_bridge_trace_dap(const uint8_t *req, const uint8_t *resp);

/* Deferred raw CMSIS-DAP passthrough (bring-up debugging): queue a request,
 * run it from the main loop, read the response back later. */
void rtt_bridge_request_raw(const uint8_t *req, uint32_t len);
uint32_t rtt_bridge_raw_result(uint8_t *out, uint32_t max);

/* 目标类型：0 = SWD/ARM，1 = RISC-V（JTAG）。HID CMD_RTT action 10 调用。 */
void rtt_bridge_set_target(uint32_t kind);

/* Stop bridging (the CDC keeps working as the UART bridge). */
void rtt_bridge_stop(void);

int rtt_bridge_is_running(void);

/* Call from the main loop. Does nothing unless the bridge is running and the
 * DAP has been idle long enough. */
void rtt_bridge_poll(void);

/* Called by the DAP command engine whenever it executes a host request, so the
 * RTT poller stays out of the way. */
void rtt_bridge_note_dap_activity(void);

/* Status/telemetry for the HID readout (also handy when debugging the bridge).
 * words[0] = running | channel<<8 | flags<<16
 * words[1] = control block address
 * words[2] = up-buffer address / size
 * words[3] = drained bytes (total)
 * words[4] = poll iterations / drains
 * words[5] = read errors / write errors
 * words[6] = bursts (transfers) / bytes moved in the last poll
 * words[7] = DAP idle gate hits / ring space left
 * Returns the number of words written (up to 8). */
uint32_t rtt_bridge_status(uint32_t *out, uint32_t words);

/* ------------------------------------------------------------------ */
/* 给 scope 采样器复用（见 src/scope/scope_sampler.c）                  */
/*                                                                     */
/* 采样器不自己写一份 SWD 初始化：那条路径里全是实测换来的细节 ——     */
/* 4 MHz 起手 → 20 MHz 斜坡换挡 → 换挡后热身 → 清 sticky → 失败返回    */
/* -4 让上层降档。两处各写一份迟早改漏一处。                           */
/* ------------------------------------------------------------------ */

/* 确保链路可用（0 = ok；语义与 rtt_swd_init 一致：-1 SWJ_Clock、-2 SWD init、-4 该档不可用） */
int      rtt_bridge_swd_ensure_ready(void);

/* 块读目标内存（0 = 成功）。内部按 s_chunk 切块，带失败重试。 */
int      rtt_bridge_read(uint32_t addr, uint8_t *dst, uint32_t len);

/* 最近一次 DAP 命令的 MCHTMR 时刻（0 = 从未执行过）——采样器用它做"让路"判断 */
uint32_t rtt_bridge_last_dap_ticks(void);

/* 运行时改 SWD 时钟档（运行中也能改；换挡走桥那套斜坡路径） */
void     rtt_bridge_set_swd_clock(uint32_t hz);
uint32_t rtt_bridge_swd_clock_hz(void);

#endif /* __RTT_BRIDGE_H__ */
