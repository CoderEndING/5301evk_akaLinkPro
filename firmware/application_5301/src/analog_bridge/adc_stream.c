/* SPDX-License-Identifier: Apache-2.0 */
#include <string.h>
#include "usb_composite.h"
#include "bus_periodic.h"
#include "analog_bridge.h"
#include "adc_stream.h"
USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX static uint8_t packet[512];
/* Shared by main-loop acquisition and USB ISR; queue/packet ownership is locked. */
static volatile uint8_t configured, enabled, started, ending, ended, busy, pending_count;
static volatile uint32_t generation;
static void word(uint8_t *p, uint32_t v) {
    for (uint8_t i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8U * i));
}
uint8_t adc_stream_enabled(void) { return enabled; }
void adc_stream_reset(uint8_t ready) {
    configured = ready;
    enabled = started = ending = ended = busy = pending_count = 0U;
    generation++;
}
uint8_t adc_stream_open(uint32_t *token) {
    uint32_t level = bp_lock();
    uint8_t rc = ANALOG_OK;
    if (!configured) rc = ANALOG_STATE;
    else if (enabled || busy || bus_periodic_owns(BP_ADC) || bus_periodic_owns(BP_I2C) ||
             bus_periodic_owns(BP_SPI) || bus_periodic_queued()) rc = ANALOG_BUSY;
    else {
        enabled = 1U; started = ending = ended = 0U;
        *token = ++generation;
    }
    bp_unlock(level); return rc;
}
uint8_t adc_stream_close(void) {
    uint32_t level = bp_lock();
    uint8_t rc = ANALOG_OK;
    if (busy || bus_periodic_owns(BP_ADC) || (enabled && !ended)) rc = ANALOG_BUSY;
    else enabled = 0U;
    bp_unlock(level); return rc;
}
void adc_stream_started(void) { if (enabled) started = 1U; }
void adc_stream_end(void) { if (enabled) ending = 1U; adc_stream_poll(); }
void adc_stream_poll(void) {
    uint32_t level = bp_lock();
    if (!enabled || !configured || busy || ended) { bp_unlock(level); return; }
    uint8_t n = bus_periodic_adc_packet(packet + 16U);
    uint8_t done = (started || ending) && !bus_periodic_owns(BP_ADC) && !n;
    if (!n && !done) { bp_unlock(level); return; }
    memcpy(packet, "ADC1", 4); packet[4] = 1U; packet[5] = done ? 2U : 1U;
    packet[6] = n; packet[7] = 26U;
    word(packet + 8U, generation); word(packet + 12U, bus_periodic_fault());
    pending_count = n; busy = 1U;
    /* 16 + 19*26 = 510 maximum: every frame is a short packet, no ZLP needed. */
    if (usbd_ep_start_write(0, ADC_IN_EP, packet, 16U + 26U * n)) {
        busy = pending_count = 0U;
    }
    bp_unlock(level);
}
void adc_stream_complete(void) {
    uint32_t level = bp_lock();
    if (busy) {
        bus_periodic_adc_ack(pending_count);
        if (packet[5] == 2U) ended = 1U;
        busy = pending_count = 0U;
    }
    bp_unlock(level);
    adc_stream_poll();
}
