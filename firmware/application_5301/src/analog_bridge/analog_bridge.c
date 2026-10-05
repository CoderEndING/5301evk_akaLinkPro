/* SPDX-License-Identifier: Apache-2.0 */
#include <string.h>
#include "board.h"
#include "clock.h"
#include "hpm_adc16_drv.h"
#include "hpm_clock_drv.h"
#include "hpm_soc.h"
#include "spi_bridge.h"
#include "led_state.h"
#include "analog_bridge.h"
/* Only board-confirmed inputs are advertised. Never commandeer debug/UART/LED pads. */
#if BOARD_HAS_VREF_ADC
#define ANALOG_CHANNEL 2U
#define ANALOG_GAIN 2U
#else
#define ANALOG_CHANNEL 3U /* EVKLite PB11, shared with SPI2. */
#define ANALOG_GAIN 1U
static uint8_t ready;
#endif
void analog_bridge_hid(uint8_t *req, uint8_t *res) {
    res[1] = 8U; res[2] = ANALOG_CMD; res[3] = req[3];
    memset(res + 4, 0, 4);
    if (req[1] < 2U || req[1] > 62U) { res[4] = ANALOG_RANGE; return; }
    if (req[3] == ANALOG_DAC_CAPS) {
        if (req[1] != 2U) { res[4] = ANALOG_RANGE; return; }
        /* Versioned reservation, no buffers, pin writes, IRQs or clock setup. */
        memset(res + 8, 0, 16); memcpy(res + 8, "DAC1", 4); res[12] = 1U;
        res[1] = 24U; return;
    }
    if (req[3] >= ANALOG_DAC_CONFIG && req[3] <= ANALOG_DAC_GET_CONFIG) {
        const uint8_t minimum[] = {10U, 5U, 12U, 11U, 7U, 3U, 3U};
        uint8_t expected = minimum[req[3] - ANALOG_DAC_CONFIG];
        if (req[3] == ANALOG_DAC_WRITE) {
            if (req[1] < 12U) { res[4] = ANALOG_RANGE; return; }
            uint8_t n = req[11];
            if (!n || n > 25U) { res[4] = ANALOG_RANGE; return; }
            expected = (uint8_t)(10U + 2U * n);
        }
        res[4] = req[1] == expected ? ANALOG_UNSUPPORTED : ANALOG_RANGE;
        return;
    }
    if (req[1] != 2U || req[3] != ANALOG_ADC_CAPS) { res[4] = ANALOG_RANGE; return; }
    memcpy(res + 8, "ANA1", 4);
    res[12] = ANALOG_CHANNEL; res[13] = 16U; res[14] = ANALOG_GAIN;
    res[15] = 0U; /* no physical DAC */
    res[16] = 3300U & 255U; res[17] = 3300U >> 8;
    res[18] = 1000U & 255U; res[19] = 1000U >> 8;
    res[1] = 20U;
}
uint8_t analog_periodic_ready(void) { return !spi_bridge_is_enabled(); }
uint8_t analog_periodic_check(const uint8_t *p, uint16_t len) {
    return len != 2U || p[0] != ANALOG_CHANNEL ||
        (p[1] != 8U && p[1] != 10U && p[1] != 12U && p[1] != 16U);
}
uint8_t analog_periodic_exec(const uint8_t *p, uint16_t len, uint8_t *data, uint8_t *n) {
    uint16_t raw = 0U; *n = 0U;
    if (analog_periodic_check(p, len)) return 1U;
    if (!analog_periodic_ready()) return 2U;
#if BOARD_HAS_VREF_ADC
    if (led_state_read_vref_raw(&raw)) return 3U;
#else
    if (!ready) {
        adc16_config_t cfg; adc16_channel_config_t ch;
        /* The board init_adc0_bus_clock() also changes CPU0: never call it here. */
        if (clock_set_adc_source(clock_adc0, clk_adc_src_ahb0) != status_success) return 3U;
        clock_add_to_group(clock_adc0, 0); adc16_get_default_config(&cfg);
        cfg.res = adc16_res_16_bits; cfg.conv_mode = adc16_conv_mode_oneshot;
        cfg.adc_clk_div = adc16_clock_divider_8; cfg.sel_sync_ahb = true;
        if (adc16_init(HPM_ADC0, &cfg) != status_success) return 3U;
        adc16_get_channel_default_config(&ch); ch.ch = ANALOG_CHANNEL; ch.sample_cycle = 20U;
        if (adc16_init_channel(HPM_ADC0, &ch) != status_success) return 3U;
        adc16_enable_oneshot_mode(HPM_ADC0); ready = 1U;
    }
    uint32_t mux = HPM_IOC->PAD[IOC_PAD_PB11].FUNC_CTL;
    HPM_IOC->PAD[IOC_PAD_PB11].FUNC_CTL = IOC_PAD_FUNC_CTL_ANALOG_MASK;
    hpm_stat_t rc = adc16_get_oneshot_result(HPM_ADC0, ANALOG_CHANNEL, &raw);
    HPM_IOC->PAD[IOC_PAD_PB11].FUNC_CTL = mux;
    if (rc != status_success) return 3U;
#endif
    /* Preserve shared ADC native resolution; selected width is output quantization. */
    raw >>= 16U - p[1]; data[0] = raw & 255U; data[1] = raw >> 8; *n = 2U;
    return 0U;
}
