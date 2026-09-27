/*
 * Copyright (c) 2021 HPMicro
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */

#include <stdio.h>
#include "board.h"
#include "hpm_debug_console.h"
#include "hpm_gpio_drv.h"
#include "hpm_soc.h"
#include "hpm_usb_drv.h"
#include "hpm_interrupt.h"
#include "usb_config.h"
#include "hpm_dfu_trigger.h"
#include "api_param.h"
#include "usb_composite.h"
#include "led_state.h"

#if BOARD_HAS_USER_KEY_DFU
/* Long-press USER KEY while the APP runs to reboot into the DFU bootloader.
 * (Holding the key at reset enters the ROM ISP mode instead, so this is the
 * only button-driven DFU entry path on this board.) */
#define DFU_KEY_HOLD_US (1000000U)
#define MCHTMR_MTIME_LO_REG (*(volatile uint32_t *)(HPM_MCHTMR_BASE + 0x00))

static void dfu_key_poll(void)
{
    static uint8_t pressed;
    static uint32_t press_start;
    static uint32_t mchtmr_freq;

    uint8_t now_pressed = (gpio_read_pin(BOARD_APP_GPIO_CTRL,
                                         BOARD_APP_GPIO_INDEX,
                                         BOARD_APP_GPIO_PIN) == BOARD_BUTTON_PRESSED_VALUE) ? 1U : 0U;
    uint32_t now = MCHTMR_MTIME_LO_REG;

    if (mchtmr_freq == 0U)
    {
        mchtmr_freq = clock_get_frequency(clock_mchtmr0);
    }

    if (now_pressed && !pressed)
    {
        press_start = now;
    }
    else if (now_pressed && ((now - press_start) > (DFU_KEY_HOLD_US / 1000000U) * mchtmr_freq))
    {
        hpm_dfu_reboot_to_dfu();
    }
    pressed = now_pressed;
}
#else
static void dfu_key_poll(void)
{
}
#endif

int main(void)
{
    board_init();
    api_param_load();

    board_init_usb((USB_Type *)CONFIG_HPM_USBD_BASE);
    intc_set_irq_priority(CONFIG_HPM_USBD_IRQn, 2);
    chry_dap_init(0, CONFIG_HPM_USBD_BASE);

    /* Bring up the UART <-> CDC COM port bridge. DAP_SETUP() above parks
     * the VCOM pins as GPIO, so this must run afterwards to mux them to
     * the CDC UART. */
    uartx_preinit();

    /* Status LEDs, external reference ADC and the periodic LED tick. */
    led_state_init();

    while (1)
    {
        chry_dap_handle();
        chry_dap_usb2uart_handle();
        api_param_poll();
        dfu_key_poll();
        /* Probe-side RTT bridge: polls the target itself (only while the DAP
         * is idle) and forwards the bytes over the CDC. */
        rtt_bridge_poll();
    }
    return 0;
}
