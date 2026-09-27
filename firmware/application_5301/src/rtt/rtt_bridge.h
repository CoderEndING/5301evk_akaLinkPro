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

/* Bring-up debugging: record the last host DAP request/response pair. */
extern volatile uint8_t g_dap_trace[64];
void rtt_bridge_trace_dap(const uint8_t *req, const uint8_t *resp);

/* Deferred raw CMSIS-DAP passthrough (bring-up debugging): queue a request,
 * run it from the main loop, read the response back later. */
void rtt_bridge_request_raw(const uint8_t *req, uint32_t len);
uint32_t rtt_bridge_raw_result(uint8_t *out, uint32_t max);

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

#endif /* __RTT_BRIDGE_H__ */
