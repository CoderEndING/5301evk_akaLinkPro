/* SPDX-License-Identifier: Apache-2.0 */
#ifndef ADC_STREAM_H
#define ADC_STREAM_H
#include <stdint.h>
/* ADC-only bulk IN. One DMA buffer, at most one native transfer in flight. */
uint8_t adc_stream_open(uint32_t *token);
uint8_t adc_stream_close(void);
uint8_t adc_stream_enabled(void);
void adc_stream_started(void);
void adc_stream_end(void);
void adc_stream_poll(void);
void adc_stream_complete(void);
void adc_stream_reset(uint8_t configured);
#endif
