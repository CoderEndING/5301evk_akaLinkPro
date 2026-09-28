/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 akaInstruments */

/*
 * Probe-side RISC-V (Debug Module) access over JTAG.
 *
 * Why this exists: a host driving RISC-V memory through OpenOCD has to pay one
 * USB round trip per abstract command (~97 us measured on this probe), which
 * caps a 32-bit read at ~96 KB/s whatever the JTAG clock is. The probe can do
 * the same job locally: one DMI transfer is a single 41-bit DR scan, so the
 * only cost left is the wire itself (~46 TCK per access).
 *
 * Transport: the DMI is behind IR = 0x11 of the TAP. After the IR is loaded
 * once, every access is one DR scan of 41 bits
 *     [0:1] op   (0 = nop, 1 = read, 2 = write)
 *     [2:33] data
 *     [34:40] address
 * and the response to a request comes back in the *next* scan, so a stream of
 * posted requests runs at one scan per access (no round trip at all).
 *
 * Bulk memory goes through the Debug Module's System Bus Access (SBA) block,
 * which auto-increments the address, so a block read is
 *     sbcs = sbaccess32 | sbautoincrement | sbreadonaddr | sbreadondata
 *     sbaddress0 = addr          -> starts the first read
 *     read sbdata0 ...           -> one word per DMI read, next read started
 * and a block write is one posted write to sbdata0 per word.
 */

#ifndef RISCV_JTAG_H
#define RISCV_JTAG_H

#include <stdint.h>

/* Open the JTAG TAP, load IR = DMI and probe the debug module.
 * Returns 0 on success, negative on failure (and fills the getters). */
int riscv_jtag_open(void);

/* Release the port (pins back to their idle state). */
void riscv_jtag_close(void);

int riscv_jtag_is_open(void);

/* Identity read back by riscv_jtag_open(). */
uint32_t riscv_jtag_idcode(void);   /* TAP IDCODE (IR = 0x01) */
uint32_t riscv_jtag_dtmcs(void);    /* DTM control/status word */
uint32_t riscv_jtag_dmstatus(void); /* Debug Module status word */

/* Single word access (32-bit aligned). Returns 0 on success. */
int riscv_jtag_read_word(uint32_t addr, uint32_t *val);
int riscv_jtag_write_word(uint32_t addr, uint32_t val);

/* Block access through the SBA. `addr` may be unaligned; returns 0 on success,
 * negative when the debug module reported an error. */
int riscv_jtag_read(uint32_t addr, uint8_t *dst, uint32_t len);
int riscv_jtag_write(uint32_t addr, const uint8_t *src, uint32_t len);

/* Timing knob: 0 = Use the compiled-in fastest timing; 1..255 = extra TCK
 * half-period delay (the JTAG engine uses the same DAP_Data.clock_delay knob
 * the SWD blobs use). */
void riscv_jtag_set_delay(uint32_t delay);
uint32_t riscv_jtag_get_delay(void);

/* Last error detail: dmstatus / sbcs observed by the failed transfer. */
uint32_t riscv_jtag_last_sbcs(void);
uint32_t riscv_jtag_last_dmstatus(void);

/* Bring-up diagnostics: raw 41-bit DR values of the most recent DMI scans. */
uint64_t riscv_jtag_dbg(uint32_t idx);

/* Reset the TAP, load IR = DMI, shift `n` NOP requests and return the raw
 * 41-bit responses (n <= 8). */
void riscv_jtag_probe(uint32_t n, uint64_t *out);

#endif /* RISCV_JTAG_H */
