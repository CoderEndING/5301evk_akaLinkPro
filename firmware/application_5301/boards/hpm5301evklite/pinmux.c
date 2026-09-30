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
 * @brief Init UART2 (PB08 TXD / PB09 RXD on J3.5/J3.3) as UART
 *
 * 2026-09-30 迁移：CDC VCOM 原来走 UART3(PB15/PB14)，但那两根脚要腾给 SPI2 的
 * IO2/IO3（四线 QSPI）。UART3 在 HPM5301 上只有 PB14/PB15 与 PA14/PA15 两组脚，
 * 后者在 QFN48 上没键合（数据手册表 45：SPI3 未引出），所以必须换实例 ——
 * UART2 的 PB08/PB09 正好在 J3 上且空闲（板上只有 R21/R22 两颗 10 k 上拉）。
 * @param None
 */
void init_uart2_pins_as_uart(void)
{
    HPM_IOC->PAD[IOC_PAD_PB08].FUNC_CTL = IOC_PB08_FUNC_CTL_UART2_TXD;
    HPM_IOC->PAD[IOC_PAD_PB09].FUNC_CTL = IOC_PB09_FUNC_CTL_UART2_RXD;
}

/*
 * for uart_lin case, need to configure pin as gpio to sent break signal
 * pull-up
 */
void init_uart2_pin_as_gpio_low(void)
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
 * @brief USB→SPI/QSPI 桥的 **SPI2** 引脚（J3 排针）
 *
 *   CS = PB10 (J3[26]，见 hw_cs)     SCLK = PB11 (J3[13])
 *   D0/MOSI = PB13 (J3[28])          D1/MISO = PB12 (J3[27])
 *   quad 时：D2/IO2 = PB14 (J3[10])、D3/IO3 = PB15 (J3[8])
 *
 * 为什么从 SPI1 搬过来（2026-09-30 实测结论）：
 *   EVKLite 的 J3 上 SPI1 的 IO2 = PA30（丝印 USB0_PWR），而那根网络经 0Ω 的 R5
 *   直接连到 AP2151(USB0 电源开关) 的 EN 节点，节点上还挂着 2N7002(Q1) 的漏极；
 *   Q1 的栅极由 CC1/CC2 → BAT54A 那套 ID 检测常态拉高 ⇒ Q1 常态导通，把整根网络
 *   （含 PA30、含 J3[37]）低阻拉到地。实测把 PA30 配成 GPIO 也拉不动、LA 上全程 0 跳变，
 *   所以 SPI1 的四线 IO2 在这块板上不可用。SPI2 的六根线在 J3 上全引出，且
 *   PB14/PB15 上原本接的板载 CH340 是 NC（原理图 U6 = NC/CH340E），正好空着。
 *
 * @param quad   1 = 把 PB14/PB15 复用成 SPI2_DAT2/DAT3（四线 QSPI）
 * @param hw_cs  1 = PB10 作硬件 CS0（每帧自动时序）；0 = 留给软件 GPIO CS
 *
 * 注意：**不要**照抄 SDK init_spi1_pins() 里除 SCLK 之外的 LOOP_BACK 位（见下）。
 */
void init_spi2_bridge_pins(uint8_t quad, uint8_t hw_cs)
{
    HPM_IOC->PAD[IOC_PAD_PB10].FUNC_CTL =
        hw_cs ? IOC_PB10_FUNC_CTL_SPI2_CS_0 : IOC_PB10_FUNC_CTL_GPIO_B_10;
    /*
     * ⚠️ PB11(SCLK) 上的 `LOOP_BACK` 位**必须留着**，这是 SDK 的
     * hpm5301evklite/init_spi1_pins() 的原样做法（只加在 SCLK 上，MISO/MOSI 都不加），
     * 换实例后结论不变。
     *
     * 它是"force input on"：把该 pad 的输入通路强制打开。SPI 主机发完时钟后，
     * **接收移位是靠 SCLK 这条输入通路回来打拍的** —— 少了它，波形看起来完全正常
     * （LA 上 8 字节 = 64 拍、MOSI 数据对、下降沿采样 MOSI=MISO=发送数据），
     * 但控制器的 RX FIFO 里读出来是**恒定的空闲电平**（CPOL=0 时全 00、CPOL=1 时全 FF），
     * 也就是"一个 bit 都没移进来"。
     *
     * P1 当初"特意不抄 LOOP_BACK"（以为是自环测试用的）就是这个坑的源头。
     * 反过来也不要乱加：SPI1 时代实测加在 PA28/PA29 上会把波形搞坏（len=4 只发 8 拍、
     * MOSI 几乎不动），所以只在 SCLK 上按 SDK 的原样加。
     */
    HPM_IOC->PAD[IOC_PAD_PB11].FUNC_CTL = IOC_PB11_FUNC_CTL_SPI2_SCLK | IOC_PAD_FUNC_CTL_LOOP_BACK_MASK;
    HPM_IOC->PAD[IOC_PAD_PB12].FUNC_CTL = IOC_PB12_FUNC_CTL_SPI2_MISO;
    HPM_IOC->PAD[IOC_PAD_PB13].FUNC_CTL = IOC_PB13_FUNC_CTL_SPI2_MOSI;

    /* 40~80 MHz 目标：这四根走 fast slew + 最大驱动 */
    const uint32_t pad_ctl = IOC_PAD_PAD_CTL_PE_SET(0) | IOC_PAD_PAD_CTL_PS_SET(0) |
                             IOC_PAD_PAD_CTL_OD_SET(0) | IOC_PAD_PAD_CTL_SR_SET(1) |
                             IOC_PAD_PAD_CTL_SPD_SET(3) | IOC_PAD_PAD_CTL_DS_SET(4);
    HPM_IOC->PAD[IOC_PAD_PB11].PAD_CTL = pad_ctl;
    HPM_IOC->PAD[IOC_PAD_PB12].PAD_CTL = pad_ctl;
    HPM_IOC->PAD[IOC_PAD_PB13].PAD_CTL = pad_ctl;

    if (quad)
    {
        HPM_IOC->PAD[IOC_PAD_PB14].FUNC_CTL = IOC_PB14_FUNC_CTL_SPI2_DAT2;
        HPM_IOC->PAD[IOC_PAD_PB15].FUNC_CTL = IOC_PB15_FUNC_CTL_SPI2_DAT3;
        HPM_IOC->PAD[IOC_PAD_PB14].PAD_CTL = pad_ctl;
        HPM_IOC->PAD[IOC_PAD_PB15].PAD_CTL = pad_ctl;
    }
}
