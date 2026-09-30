/*
 * Copyright (c) 2023-2026 HPMicro
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Board: HPM5301EVKLite (akaLinkPro DAP firmware port)
 *
 * DAP target-side pins are wired to the J5 20-pin JTAG header:
 *   J5.5  TDI   -> PA05
 *   J5.7  TMS   -> PA07 (SWDIO, single-pin bidirectional)
 *   J5.9  TCK   -> PA06 (SWCLK)
 *   J5.13 TDO   -> PA04
 *   J5.3  nTRST -> PA08 (driven as the DAP nRESET output)
 *   J5.15 nSRST -> chip RESET_N (input only, cannot be driven by firmware)
 *
 * The same pins are the SoC's own JTAG port, so the board stays debuggable
 * (via J5 / the onboard FT2232) whenever the APP is not running (DFU
 * bootloader mode / ISP mode).
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

#define BOARD_NAME "hpm5301evklite"
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

/* User button: USER KEY on PA03 (pressed = high, pad has pull-down).
 * Note: holding it at reset enters the ROM ISP mode; while the APP runs it
 * is a plain GPIO (long-press enters DFU mode, see main.c). */
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

/* ------------------------------------------------------------------ */
/* DAP target-side (debug port) pins, all on the J5 header.            */
/* ------------------------------------------------------------------ */
#define BOARD_PIN_JTCK          IOC_PAD_PA06
#define BOARD_PIN_JTMS          IOC_PAD_PA07
#define BOARD_PIN_JTDI          IOC_PAD_PA05
#define BOARD_PIN_JTDO          IOC_PAD_PA04
#define BOARD_PIN_nRESET        IOC_PAD_PA08

/* CDC VCOM bridge: UART3 on PB15 (TXD, J3.8) / PB14 (RXD, J3.10) - the pins
 * labelled UART_TXD/UART_RXD on the EVKLite J3 header. Independent from the
 * JTAG pins - no UART/JTAG pin muxing needed. */
#define BOARD_PIN_UART_TXD      IOC_PAD_PB15
#define BOARD_PIN_UART_RXD      IOC_PAD_PB14
#define BOARD_UART_TX_FUNC      IOC_PB15_FUNC_CTL_UART3_TXD
#define BOARD_UART_RX_FUNC      IOC_PB14_FUNC_CTL_UART3_RXD
#define BOARD_CDC_UART_BASE     HPM_UART3
#define BOARD_CDC_UART_IRQ      IRQn_UART3
#define BOARD_CDC_UART_CLK_NAME clock_uart3
#define BOARD_CDC_UART_RX_DMA   HPM_DMA_SRC_UART3_RX
#define BOARD_CDC_UART_TX_DMA   HPM_DMA_SRC_UART3_TX

/* Status LED: single board LED on PA10, active low. */
#define BOARD_LED1_PIN          IOC_PAD_PA10
#define BOARD_LED2_PIN          IOC_PAD_PA10

#define PIN_GPIOM_BASE    HPM_GPIOM
#define PIN_GPIO          HPM_FGPIO
#define PIN_GPIOM         gpiom_core0_fast

/* ------------------------------------------------------------------ */
/* Board feature flags consumed by the shared application sources.     */
/* ------------------------------------------------------------------ */
/* SWD bit-bang blobs pre-generated for SWCLK=PA06 / SWDIO=PA07. */
#define BOARD_SWD_BLOB_EVKLITE (1)
/* The EVKLite wires SWDIO directly to PA07: single bidirectional pin, no
 * external level shifter, no direction pin. */
#define BOARD_HAS_SWDIO_DIR (0)
/* No dedicated JTAG nTRST output pin (PA31 is a plain J3 header pin here). */
#define BOARD_HAS_JTRST (0)
/* nRESET (PA08) drives the target reset line directly, active low. */
#define BOARD_NRESET_ACTIVE_LOW (1)
/* No 5V level-shifter supply and no external VREF divider on this board. */
#define BOARD_HAS_5V_EN (0)
#define BOARD_HAS_VREF_ADC (0)
/* LED PA10 is active low. */
#define BOARD_LED_ACTIVE_LOW (1)
/* UART2 pins (PB08/PB09) do not overlap the JTAG pins. */
#define BOARD_UART2_SHARES_JTAG_PINS (0)
/* No board pin needs to be parked when the JTAG engine owns the pads. */
/* BOARD_JTAG_PARK_PIN intentionally not defined. */
/* Long-press USER KEY (PA03) while running enters DFU mode. */
#define BOARD_HAS_USER_KEY_DFU (1)
/* No DTR/RTS nets on this board. */
#define BOARD_UART_DTR_PAD (0U)
#define BOARD_UART_RTS_PAD (0U)

/* USB→SPI/QSPI 转发桥：J3 排针上引出完整 SPI1（含 quad 的 PA30/PA31），见
 * docs/usb-spi-bridge-plan.md。其他板子（akaLinkPro）没这套排针定义，置 0。 */
#define BOARD_HAS_SPI_BRIDGE (1)

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
