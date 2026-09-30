/*
 * Copyright (c) 2023 HPMicro
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */

#ifndef HPM_PINMUX_H
#define HPM_PINMUX_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

void init_py_pins_as_pgpio(void);
void init_uart0_pins(void);
void init_button_pins(void);
void init_usb0_pins(void);
void init_power_pins(void);
void board_set_5v_output(uint8_t on);
void init_led_pins(void);
void init_adc_vref_pin(void);

void init_uart2_pins_as_uart(void);
void init_uart2_pin_as_gpio_low(void);
void init_unused_pin_as_input(void);
void init_jtag_swd_pin(void);

/* USB→SPI/QSPI 桥：**SPI2** 引脚（J3 排针），quad=1 时启用 PB14/PB15 作 IO2/IO3，
 * hw_cs=1 时 PB10 作硬件 CS0，否则留给软件 GPIO CS。
 * （SPI1 的 IO2=PA30 被板上 Q1 短到地，四线用不了，见 pinmux.c 的说明。） */
void init_spi2_bridge_pins(uint8_t quad, uint8_t hw_cs);

#ifdef __cplusplus
}
#endif
#endif /* HPM_PINMUX_H */
