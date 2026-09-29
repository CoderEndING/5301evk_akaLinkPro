/*
 * Copyright (c) 2026 HPMicro
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * HPM5301EVKLite pinmux for the akaLinkPro DAP firmware.
 */

/*
 * Note:
 * PY and PZ IOs: if any SOC pin function needs to be routed to these IOs,
 * besides of IOC, PIOC/BIOC needs to be configured SOC_GPIO_X_xx, so that
 * expected SoC function can be enabled on these IOs.
 */

#include "pinmux.h"
#include "board.h"
#include "hpm_trgm_drv.h"
#include "hpm_gpio_drv.h"
#include "hpm_gpiom_drv.h"

static void gpiom_config_pin_to_gpio0(uint16_t gpio_index)
{
    gpiom_set_pin_controller(HPM_GPIOM,
                             GPIO_GET_PORT_INDEX(gpio_index),
                             GPIO_GET_PIN_INDEX(gpio_index),
                             gpiom_soc_gpio0);
    gpiom_enable_pin_visibility(HPM_GPIOM,
                                GPIO_GET_PORT_INDEX(gpio_index),
                                GPIO_GET_PIN_INDEX(gpio_index),
                                gpiom_soc_gpio0);
}

/**
 * @brief Set PY00-PY01 default function to PGPIO
 * @param None
 */
void init_py_pins_as_pgpio(void)
{
    HPM_PIOC->PAD[IOC_PAD_PY00].FUNC_CTL = PIOC_PY00_FUNC_CTL_PGPIO_Y_00;
    HPM_PIOC->PAD[IOC_PAD_PY01].FUNC_CTL = PIOC_PY01_FUNC_CTL_PGPIO_Y_01;
}

/**
 * @brief Init UART0 Console print pin
 * @param None
 */
void init_uart0_pins(void)
{
    HPM_IOC->PAD[IOC_PAD_PA00].FUNC_CTL = IOC_PA00_FUNC_CTL_UART0_TXD;
    HPM_IOC->PAD[IOC_PAD_PA01].FUNC_CTL = IOC_PA01_FUNC_CTL_UART0_RXD;
}

/*
 * configure pad setting: pull enable and pull down, schmitt trigger enable
 * enable schmitt trigger to eliminate jitter of pin used as button
 */
void init_button_pins(void)
{
    /* USER KEY on PA03 (pressed = high) */
    HPM_IOC->PAD[IOC_PAD_PA03].FUNC_CTL = IOC_PAD_FUNC_CTL_ALT_SELECT_SET(0);
    HPM_IOC->PAD[IOC_PAD_PA03].PAD_CTL = IOC_PAD_PAD_CTL_HYS_SET(1) | IOC_PAD_PAD_CTL_PE_SET(1) | IOC_PAD_PAD_CTL_PS_SET(0);
}

/**
 * @brief 5V level-shifter control is not wired on this board (no-op).
 * @param on ignored
 */
void board_set_5v_output(uint8_t on)
{
    (void)on;
}

/**
 * @brief Power control pins: none on this board (no-op, kept for API parity).
 * @param None
 */
void init_power_pins(void)
{
}

/**
 * @brief Init the status LED pin (PA10) as push-pull GPIO output, off.
 * The board LED is active low, so "off" drives the pin high.
 * @param None
 */
void init_led_pins(void)
{
    HPM_IOC->PAD[BOARD_LED1_PIN].FUNC_CTL = IOC_PAD_FUNC_CTL_ALT_SELECT_SET(0);
    HPM_IOC->PAD[BOARD_LED1_PIN].PAD_CTL =
        IOC_PAD_PAD_CTL_HYS_SET(0) | // schmitt trigger disable
        IOC_PAD_PAD_CTL_PRS_SET(0) | // pull resistor 100k
        IOC_PAD_PAD_CTL_PS_SET(0) |  // pull down
        IOC_PAD_PAD_CTL_PE_SET(0) |  // pull disable
        IOC_PAD_PAD_CTL_KE_SET(0) |  // keeper disable
        IOC_PAD_PAD_CTL_OD_SET(0) |  // open drain disable
        IOC_PAD_PAD_CTL_SR_SET(1) |  // fast slew rate
        IOC_PAD_PAD_CTL_SPD_SET(3) | // fastest slew rate
        IOC_PAD_PAD_CTL_DS_SET(4);   // drive strength 39 ohm(3.3V)

    gpiom_config_pin_to_gpio0(BOARD_LED1_PIN);
    gpio_set_pin_output(HPM_GPIO0, GPIO_GET_PORT_INDEX(BOARD_LED1_PIN), GPIO_GET_PIN_INDEX(BOARD_LED1_PIN));
    gpio_write_pin(HPM_GPIO0, GPIO_GET_PORT_INDEX(BOARD_LED1_PIN), GPIO_GET_PIN_INDEX(BOARD_LED1_PIN), 1);
}

/**
 * @brief External reference sense pin: not wired on this board (no-op).
 * @param None
 */
void init_adc_vref_pin(void)
{
}

/**
 * @brief Init USB0 pins
 * @param None
 */
void init_usb0_pins(void)
{
    HPM_IOC->PAD[IOC_PAD_PA24].FUNC_CTL = IOC_PAD_FUNC_CTL_ANALOG_MASK;

    HPM_IOC->PAD[IOC_PAD_PA25].FUNC_CTL = IOC_PAD_FUNC_CTL_ANALOG_MASK;

    /* USB0_ID */
    HPM_IOC->PAD[IOC_PAD_PY00].FUNC_CTL = IOC_PY00_FUNC_CTL_USB0_ID;
    HPM_PIOC->PAD[IOC_PAD_PY00].FUNC_CTL = PIOC_PY00_FUNC_CTL_SOC_GPIO_Y_00;

    /* USB0_OC */
    HPM_IOC->PAD[IOC_PAD_PY01].FUNC_CTL = IOC_PY01_FUNC_CTL_USB0_OC;
    HPM_PIOC->PAD[IOC_PAD_PY01].FUNC_CTL = PIOC_PY01_FUNC_CTL_SOC_GPIO_Y_01;
}

/**
 * @brief Init UART3 (PB15 TXD / PB14 RXD on J3.8/J3.10) as UART
 * @param None
 */
void init_uart3_pins_as_uart(void)
{
    HPM_IOC->PAD[IOC_PAD_PB15].FUNC_CTL = IOC_PB15_FUNC_CTL_UART3_TXD;
    HPM_IOC->PAD[IOC_PAD_PB14].FUNC_CTL = IOC_PB14_FUNC_CTL_UART3_RXD;
}

/*
 * for uart_lin case, need to configure pin as gpio to sent break signal
 * pull-up
 */
void init_uart3_pin_as_gpio_low(void)
{
}

/*
 * @brief Init unused pins as input
 * @param None
 */
void init_unused_pin_as_input(void)
{
}

/**
 * @brief Init JTAG SWD pins as default state
 * @param None
 */
void init_jtag_swd_pin(void)
{
}

/**
 * @brief USB→SPI/QSPI 桥的 SPI1 引脚（J3 排针）
 *
 *   SCLK = PA27 (J3[23])       MOSI/IO0 = PA29 (J3[19])
 *   MISO/IO1 = PA28 (J3[21])   CS = PA26 (J3[24]，见 hw_cs)
 *   quad 时：IO2 = PA30 (J3[37])、IO3 = PA31 (J3[11])
 *
 * @param quad   1 = 把 PA30/PA31 复用成 SPI1_DAT2/DAT3（四线 QSPI）
 * @param hw_cs  1 = PA26 作硬件 CS0（每帧自动时序）；0 = 留给软件 GPIO CS
 *
 * 注意：**不要**照抄 SDK init_spi1_pins() 里的 LOOP_BACK 位（那是 SPI 自环测试用的，
 * 正常通信会把输出环回进输入）。
 */
void init_spi1_bridge_pins(uint8_t quad, uint8_t hw_cs)
{
    HPM_IOC->PAD[IOC_PAD_PA26].FUNC_CTL =
        hw_cs ? IOC_PA26_FUNC_CTL_SPI1_CS_0 : IOC_PA26_FUNC_CTL_GPIO_A_26;
    HPM_IOC->PAD[IOC_PAD_PA27].FUNC_CTL = IOC_PA27_FUNC_CTL_SPI1_SCLK;
    HPM_IOC->PAD[IOC_PAD_PA28].FUNC_CTL = IOC_PA28_FUNC_CTL_SPI1_MISO;
    HPM_IOC->PAD[IOC_PAD_PA29].FUNC_CTL = IOC_PA29_FUNC_CTL_SPI1_MOSI;

    /* 40~80 MHz 目标：这四根走 fast slew + 最大驱动 */
    const uint32_t pad_ctl = IOC_PAD_PAD_CTL_PE_SET(0) | IOC_PAD_PAD_CTL_PS_SET(0) |
                             IOC_PAD_PAD_CTL_OD_SET(0) | IOC_PAD_PAD_CTL_SR_SET(1) |
                             IOC_PAD_PAD_CTL_SPD_SET(3) | IOC_PAD_PAD_CTL_DS_SET(4);
    HPM_IOC->PAD[IOC_PAD_PA27].PAD_CTL = pad_ctl;
    HPM_IOC->PAD[IOC_PAD_PA28].PAD_CTL = pad_ctl;
    HPM_IOC->PAD[IOC_PAD_PA29].PAD_CTL = pad_ctl;

    if (quad)
    {
        HPM_IOC->PAD[IOC_PAD_PA30].FUNC_CTL = IOC_PA30_FUNC_CTL_SPI1_DAT2;
        HPM_IOC->PAD[IOC_PAD_PA31].FUNC_CTL = IOC_PA31_FUNC_CTL_SPI1_DAT3;
        HPM_IOC->PAD[IOC_PAD_PA30].PAD_CTL = pad_ctl;
        HPM_IOC->PAD[IOC_PAD_PA31].PAD_CTL = pad_ctl;
    }
}
