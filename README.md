# akaLinkPro

akaLinkPro 是一个基于 HPM5301 的高性能 CMSIS-DAP 调试器。同一套应用源码支持两块硬件：

| 板级 | 板级目录 | DAP 目标侧输出 | CDC 虚拟串口 |
| --- | --- | --- | --- |
| akaLinkPro（原板） | `firmware/*/boards/akaLinkPro` | 板载排针 | UART2，PA08/PA09 |
| HPM5301EVKLite（移植） | `firmware/*/boards/hpm5301evklite` | J5 20 针 JTAG 座 | UART3，PB15/PB14 = J3.8/J3.10 |

- 主固件：`firmware/application_5301`
- DFU/MSC Bootloader：`firmware/bootloader_dfu`
- 配置上位机（WebHID）：`docs/index.html`
- USB 标识：APP `VID_0D28 PID_0204`（CMSIS-DAP + CDC + HID + WebUSB + DFU Runtime），
  DFU `VID_0D28 PID_0207`（DFU + MSC 虚拟 U 盘 `AKALINKPRO`）

板级通过 `board.h` 中的特性宏适配（`BOARD_UART2_SHARES_JTAG_PINS`、
`BOARD_HAS_SWDIO_DIR`、`BOARD_NRESET_ACTIVE_LOW`、`BOARD_LED_ACTIVE_LOW`、
`BOARD_HAS_VREF_ADC`、`BOARD_SWD_BLOB_EVKLITE` 等），两块板共用同一套 `src/`。

## HPM5301EVKLite 引脚与接线

### DAP 目标调试口（J5，同时是芯片自身 JTAG）

| 信号 | HPM5301 | J5 | 说明 |
| --- | --- | --- | --- |
| SWCLK / TCK | PA06 | J5.9 | FGPIO 位带输出 |
| SWDIO / TMS | PA07 | J5.7 | 单脚双向（无方向控制脚） |
| TDI | PA05 | J5.5 | 仅 JTAG 模式 |
| TDO | PA04 | J5.13 | 仅 JTAG 模式 |
| nRESET（目标复位） | PA08 | J5.3 | 直出低有效 |
| VTref / GND | — | J5.1 / J5.4·6·8… | 板上 3.3V |

> ⚠️ **J5.15 是这块板子自己的 RESET_N**，固件无法驱动它。用 20 针排线直连目标板
> 时别让目标板的复位网络反灌 J5.15；SWD 目标建议直接用杜邦线取上表信号。

### 板载资源

| 功能 | 引脚 | 位置 | 说明 |
| --- | --- | --- | --- |
| CDC 虚拟串口 TXD | PB15 | J3.8 | 板上丝印 `UART_TXD`，UART3 |
| CDC 虚拟串口 RXD | PB14 | J3.10 | 板上丝印 `UART_RXD`，UART3 |
| UART0 console | PA00 / PA01 | J3.36 / J3.38 | `printf` 调试口，115200-8N1 |
| USER KEY | PA03 | 板载按键 | 按下为高；**上电时按住进 ROM ISP**，运行时长按 1s 进 DFU |
| 状态 LED | PA10 | 板载 LED2 | 低电平点亮 |
| USB0 OTG（上行口） | PA24 / PA25 | J1 Type-C | USB 2.0 高速设备 |

J3.3 / J3.5（PB09 / PB08，丝印 I2C_SDA/SCL）在部分固件里曾是 VCOM，现在是空闲
I2C 脚；要换回去只改板级 `board.h` 的 `BOARD_PIN_UART_TX/RX` 与
`BOARD_CDC_UART_*` 即可，应用源码不用动。

### 串口回环测试接线

**必须把 J3.8(TXD) 和 J3.10(RXD) 实际短接**——接逻辑分析仪不算回环。固件侧
（引脚复用、时钟、DMA、CDC 桥）已验证正常，测不到回显时优先查这两根线。

```bat
:: EVKLite CDC 在设备管理器里是复合设备的 MI_01（"USB 串行设备 (COMx)"）
make uart-echo COM=COM7     :: 写 27 字节比对回环
make uart-loop COM=COM7     :: 9600 ~ 10 Mbps 全速率扫描
```

实测：9600 ~ 10 Mbps 全部通过，10 Mbps → 974 KB/s（线速效率 99.7%）。

## 构建

依赖 HPM SDK 1.11.0 环境（含 `rv32imac_zicsr_zifencei_multilib_b_ext-win` 工具链），
默认路径 `E:\sdk_env_v1.11.0`，用环境变量 `HPM_SDK_ENV_DIR` 覆盖。

```bat
make build          :: bootloader + APP
make build-boot     :: 仅 DFU bootloader -> build_xip_evklite\
make build-app      :: 仅 APP（DFU 布局）-> build_dfu_evklite\，产出 _pack.bin/_pack.hex
make clean
```

也可以直接跑板级脚本：`firmware/application_5301/build_dfu_evklite.bat`、
`firmware/bootloader_dfu/build_xip_evklite.bat`。

> **SDK 1.11 兼容说明**：新版 SDK 移除了 `flash_dfu` 构建类型，EVKLite 脚本改用
> `HPM_BUILD_TYPE=flash_xip` + 自定义链接脚本 `linker/flash_dfu_app.ld`，并由 CMake
> 追加 `-DFLASH_XIP=0 -DFLASH_DFU=1`，得到与原 `flash_dfu` 一致的镜像布局
> （APP 头在 `0x80020000`，入口 `0x80020100`）。
>
> APP 固件区尾部两个 4K 扇区（`0x800FE000`/`0x800FF000`）留给参数存储
> （EasyFlash），构建已把 `_flash_size` 收窄 8K，不会被代码占用。

## 烧录

| 场景 | 方法 |
| --- | --- |
| 首次烧录（空片） | J-Link 接 J5：`make flash`（bootloader + APP 一次烧完） |
| 日常只更新 APP | `make flash-app`（J-Link，保留 bootloader） |
| DFU 升级 | 长按 USER KEY 1s 或发 HID `CMD_ENTER_DFU(0xFF)` → 把 `akaLinkPro_App_pack.bin` 拖进虚拟 U 盘 |
| dfu-util | `make dfu`（需自行安装 dfu-util 并加入 PATH） |
| 救砖 | 按住 USER KEY 上电/复位进 ROM ISP，用 hpm_manufacturing_tool 经 USB/UART0 下载 |

> **烧录前提**：APP 运行时 PA04–PA08 被 DAP 占用（就是芯片自身的 JTAG 脚），
> J-Link 连不上，必须先让板子进入 DFU / ISP 模式。
>
> DFU 虚拟盘写入偶尔不触发 bootloader 提交，重新写一次文件即可。

## 测试

```bat
make sram-test   :: STM32F103 SRAM 读写测速（CMSIS-DAP + OpenOCD，1~60 MHz，逐字节校验）
make rtt-test    :: STM32F103 SEGGER RTT 吞吐
make uart-echo   :: EVKLite CDC 回环快检（先短接 J3.8 <-> J3.10）
make uart-loop   :: EVKLite CDC 全速率回环扫描
```

`make sram-test` 实测（20 KB @ `0x20000000`，OpenOCD `load_image`/`dump_image`）：

| SWD 时钟 | 写 | 读 |
| --- | --- | --- |
| 1 MHz | 85.5 KB/s | 85.9 KB/s |
| 10 MHz | 749.5 KB/s | 770.5 KB/s |
| 36 MHz | 2081.2 KB/s | 1903.0 KB/s |
| 60 MHz | 2847.6 KB/s | 2566.6 KB/s |

脚本说明见 [`script_test/README.md`](script_test/README.md)；`sram/rtt` 脚本的工具路径
可用 `OPENOCD_EXE`、`OPENOCD_SCRIPTS`、`HPM_SDK_ENV_DIR` 覆盖。

## 文档

| 文档 | 内容 |
| --- | --- |
| [`docs/HPM5301EVKLite_port.md`](docs/HPM5301EVKLite_port.md) | EVKLite 移植说明：引脚映射、构建、烧录、自调试、验证清单 |
| [`docs/HANDOVER-evklite-20260927.md`](docs/HANDOVER-evklite-20260927.md) | 移植过程交接记录（含 CDC 回环故障的根因与修复） |
| [`firmware/application_5301/Custom HID Protocol.md`](firmware/application_5301/Custom%20HID%20Protocol.md) | HID 配置协议 |
| [`firmware/application_5301/Flash_Memory_Map.md`](firmware/application_5301/Flash_Memory_Map.md) | Flash 布局 |
| [`firmware/application_5301/Firmware_Integrity_Plan.md`](firmware/application_5301/Firmware_Integrity_Plan.md) | 固件头/CRC 校验设计 |
| [`.opencode/skills/akalinkpro-firmware/SKILL.md`](.opencode/skills/akalinkpro-firmware/SKILL.md) | 构建 / 烧录 / 调试技能说明 |
| [`docs/HPM5301EVKLite_UG_V1.1.pdf`](docs/HPM5301EVKLite_UG_V1.1.pdf) | 先楫官方 EVKLite 用户手册（J3/J5 引脚定义出处） |

## License

本项目采用 **Apache License 2.0** 许可，详见根目录 [`LICENSE`](LICENSE)。

```
Copyright (c) 2026 akaInstruments

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0
```

## 第三方组件

本项目在源码与构建中使用/参考了以下第三方组件，其版权归各自作者所有，
并按其各自的许可证分发（这些组件的原始许可证声明请见对应文件/目录）：

| 组件 | 用途 | 许可证 |
| --- | --- | --- |
| [HPMicro HPM SDK](https://github.com/hpmicro/hpm_sdk) | HPM5301 SoC/驱动/启动/链接脚本 | BSD-3-Clause |
| [ARM CMSIS-DAP](https://github.com/ARM-software/CMSIS-DAP) | CMSIS-DAP 命令引擎（`src/dap/`） | Apache-2.0 |
| [CherryUSB](https://github.com/cherry-embedded/CherryUSB) | USB 设备协议栈 | Apache-2.0 |
| [CherryRB](https://github.com/cherry-embedded/CherryRB) | 无锁环形缓冲区 | Apache-2.0 |
| [EasyFlash](https://github.com/armink/EasyFlash) | 参数持久化（`src/easyflash/`） | MIT |

> 若再分发二进制，请一并保留上述组件的版权与许可证声明。
