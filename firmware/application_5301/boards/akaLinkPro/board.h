/*
 * Copyright (c) 2023-2026 HPMicro
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */

#ifndef _HPM_BOARD_H
#define _HPM_BOARD_H
#include <stdio.h>
#include <stdarg.h>
#include "hpm_common.h"
#include "hpm_clock_drv.h"
#include "hpm_soc.h"
#include "hpm_soc_feature.h"
#include "pinmux.h"
#if !defined(CONFIG_NDEBUG_CONSOLE) || !CONFIG_NDEBUG_CONSOLE
#include "hpm_debug_console.h"
#endif

#define BOARD_NAME "akaLinkPro"
#define BOARD_UF2_SIGNATURE (0x0A4D5048UL)
#define BOARD_DFU_SIGNATURE (0x48504D21UL)
#define BOARD_BGPR HPM_BGPR0

/* core section */
#ifndef BOARD_RUNNING_CORE
#define BOARD_RUNNING_CORE HPM_CORE0
#endif

/* uart section */
#ifndef BOARD_APP_UART_BASE
#define BOARD_APP_UART_BASE HPM_UART3
#define BOARD_APP_UART_IRQ IRQn_UART3
#define BOARD_APP_UART_BAUDRATE (115200UL)
#define BOARD_APP_UART_CLK_NAME clock_uart3
#define BOARD_APP_UART_RX_DMA_REQ HPM_DMA_SRC_UART3_RX
#define BOARD_APP_UART_TX_DMA_REQ HPM_DMA_SRC_UART3_TX
#endif

#define BOARD_APP_UART_BREAK_SIGNAL_PIN IOC_PAD_PA26

#if !defined(CONFIG_NDEBUG_CONSOLE) || !CONFIG_NDEBUG_CONSOLE
#ifndef BOARD_CONSOLE_TYPE
#define BOARD_CONSOLE_TYPE CONSOLE_TYPE_UART
#endif

#if BOARD_CONSOLE_TYPE == CONSOLE_TYPE_UART
#ifndef BOARD_CONSOLE_UART_BASE
#define BOARD_CONSOLE_UART_BASE HPM_UART0
#define BOARD_CONSOLE_UART_CLK_NAME clock_uart0
#define BOARD_CONSOLE_UART_IRQ IRQn_UART0
#define BOARD_CONSOLE_UART_TX_DMA_REQ HPM_DMA_SRC_UART0_TX
#define BOARD_CONSOLE_UART_RX_DMA_REQ HPM_DMA_SRC_UART0_RX
#endif
#define BOARD_CONSOLE_UART_BAUDRATE (115200UL)
#endif
#endif

/* nor flash section */
#define BOARD_FLASH_BASE_ADDRESS (0x80000000UL) /* Check */
#define BOARD_FLASH_SIZE (SIZE_1MB)

/* XPI NOR configuration for ROM API (same as the DFU bootloader). */
#define BOARD_APP_XPI_NOR_XPI_BASE (HPM_XPI0)
#define BOARD_APP_XPI_NOR_CFG_OPT_HDR (0xfcf90002U)
#define BOARD_APP_XPI_NOR_CFG_OPT_OPT0 (0x00000006U)
#define BOARD_APP_XPI_NOR_CFG_OPT_OPT1 (0x00001000U)

/* Configuration store: the last two 4K sectors of the 1MB QSPI NOR
 * (0x800FE000 - 0x800FFFFF) are reserved for the APP-owned parameter store.
 * They sit at the tail of the APP region, never in the bootloader region. */
#define BOARD_PARAM_RESERVED_SIZE (0x2000UL)

/* gpiom section */
#define BOARD_APP_GPIOM_BASE HPM_GPIOM
#define BOARD_APP_GPIOM_USING_CTRL HPM_FGPIO
#define BOARD_APP_GPIOM_USING_CTRL_NAME gpiom_core0_fast

/* User button */
#define BOARD_APP_GPIO_CTRL HPM_GPIO0
#define BOARD_APP_GPIO_INDEX GPIO_DI_GPIOA
#define BOARD_APP_GPIO_PIN 3
#define BOARD_APP_GPIO_IRQ IRQn_GPIO0_A
#define BOARD_BUTTON_PRESSED_VALUE 1

#ifndef BOARD_SHOW_CLOCK
#define BOARD_SHOW_CLOCK 1
#endif
#ifndef BOARD_SHOW_BANNER
#define BOARD_SHOW_BANNER 1
#endif


#define BOARD_PIN_UART_TXD      IOC_PAD_PA08
#define BOARD_PIN_UART_RXD      IOC_PAD_PA09
#define BOARD_UART_TX_FUNC      IOC_PA08_FUNC_CTL_UART2_TXD
#define BOARD_UART_RX_FUNC      IOC_PA09_FUNC_CTL_UART2_RXD
#define BOARD_CDC_UART_BASE     HPM_UART2
#define BOARD_CDC_UART_IRQ      IRQn_UART2
#define BOARD_CDC_UART_CLK_NAME clock_uart2
#define BOARD_CDC_UART_RX_DMA   HPM_DMA_SRC_UART2_RX
#define BOARD_CDC_UART_TX_DMA   HPM_DMA_SRC_UART2_TX

#define BOARD_PIN_JTCK          IOC_PAD_PA27
#define BOARD_PIN_JTMS          IOC_PAD_PA28
// #define BOARD_PIN_JTMS_OUT      IOC_PAD_PA29
#define BOARD_PIN_JTMS_DIR      IOC_PAD_PA30
#define BOARD_PIN_JTDO          IOC_PAD_PA09
#define BOARD_PIN_JTDI          IOC_PAD_PA08
#define BOARD_PIN_JTRST         IOC_PAD_PA31
#define BOARD_PIN_nRESET        IOC_PAD_PA26

#define BOARD_LED1_PIN          IOC_PAD_PB11
#define BOARD_LED2_PIN          IOC_PAD_PB12

#define PIN_GPIOM_BASE    HPM_GPIOM
#define PIN_GPIO          HPM_FGPIO
#define PIN_GPIOM         gpiom_core0_fast

/* ------------------------------------------------------------------ */
/* Board feature flags consumed by the shared application sources.     */
/* (akaLinkPro hardware: external level shifter + JTAG/SWD mux)        */
/* ------------------------------------------------------------------ */
/* SWD bit-bang blobs use the akaLinkPro pin mapping (SWCLK=PA27/SWDIO=PA28). */
#define BOARD_SWD_BLOB_EVKLITE (0)
/* SWDIO is level-shifted: PA30 controls the buffer direction. */
#define BOARD_HAS_SWDIO_DIR (1)
/* Dedicated nTRST output on PA31. */
#define BOARD_HAS_JTRST (1)
/* nRESET (PA26) drives the target reset through an inverting transistor,
 * so the firmware-level polarity is inverted (release = drive low). */
#define BOARD_NRESET_ACTIVE_LOW (0)
/* 5V level-shifter supply on PB13 and VREF divider on PB10 (ADC0.2). */
#define BOARD_HAS_5V_EN (1)
#define BOARD_HAS_VREF_ADC (1)
/* LEDs PB11/PB12 are active high. */
#define BOARD_LED_ACTIVE_LOW (0)
/* UART2 (PA08/PA09) shares the pins with JTAG TDI/TDO. */
#define BOARD_UART2_SHARES_JTAG_PINS (1)
/* PA10 is parked (output low) while the JTAG engine owns the pads. */
#define BOARD_JTAG_PARK_PIN IOC_PAD_PA10
/* No long-press DFU entry on this board (dedicated DFU pin instead). */
#define BOARD_HAS_USER_KEY_DFU (0)
/* DTR/RTS nets exist on the schematic but are not driven by default. */
#define BOARD_UART_DTR_PAD (IOC_PAD_PA06)
#define BOARD_UART_RTS_PAD (IOC_PAD_PA07)


#if defined(__cplusplus)
extern "C"
{
#endif /* __cplusplus */

    typedef void (*board_timer_cb)(void);

    void board_init_gpio_pins(void);
    void board_init_usb(USB_Type *ptr);
    void board_init_console(void);
    void board_init_uart(UART_Type *ptr);

    void board_init(void);
    void board_init_usb_dp_dm_pins(void);
    void board_init_clock(void);
    void board_delay_us(uint32_t us);
    void board_delay_ms(uint32_t ms);
    void board_ungate_mchtmr_at_lp_mode(void);

    void board_init_pmp(void);
    uint32_t board_init_uart_clock(UART_Type *ptr);
    void init_uart_pins(UART_Type *ptr);
    void init_usb_pins(USB_Type *ptr);
#if defined(__cplusplus)
}
#endif /* __cplusplus */
#endif /* _HPM_BOARD_H */
