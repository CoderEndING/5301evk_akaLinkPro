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
make rtt-test    :: STM32F103 SEGGER RTT 吞吐（OpenOCD rtt server）
make rtt-max     :: RTT 取数上限（轮询跑在 OpenOCD 内部，无 telnet 往返）
make rtt-link    :: 目标运行中 SWD 读的可靠性矩阵
make uart-echo   :: EVKLite CDC 回环快检（先短接 J3.8 <-> J3.10）
make uart-loop   :: EVKLite CDC 全速率回环扫描
```

`make sram-test` 实测（20 KB @ `0x20000000`，OpenOCD `load_image`/`dump_image`，
目标已归一到 64 MHz）：

| SWD 时钟 | 写 | 读 |
| --- | --- | --- |
| 1 MHz | 85.4 KB/s | 85.8 KB/s |
| 10 MHz | 748.1 KB/s | 768.7 KB/s |
| 36 MHz | 1958.7 KB/s | 1944.6 KB/s |
| 60 MHz | 2640.3 KB/s | 2375.7 KB/s |

> ⚠️ 这个数字受**目标机主频**限制（每次 SWD AHB-AP 事务要花几个目标 HCLK）：
> STM32F103 上电默认 HSI 8 MHz 时，无论 SWD 时钟拉到多高都会卡在 ~1.4 MB/s，
> 而 F1 的 `reset halt` 是核心级复位、不清 RCC，所以数字会随目标上电后的状态
> 变化一倍。`sram_speed_test.py` 因此默认在每轮复位后把目标提到 64 MHz 并打印
> 实测时钟（`--no-boost` 可关闭）。

### SEGGER RTT 吞吐（同一块探针，为什么不是 2 MB/s）

RTT 是**主机轮询**模型：每次取数要 3 个 host↔探针来回（读 WrOff/RdOff → 读环形
缓冲 → 写回 RdOff），而 SRAM 测速是一次大块流水传输，所以两者不可比。实测阶梯
（目标均归一到 64 MHz）：

| 配置 | 吞吐 |
| --- | --- |
| 目标停在复位默认 8 MHz（RTT 生产者在目标侧，此时封顶） | 277 KB/s |
| `make rtt-test`（OpenOCD rtt server） | **919 KB/s** |
| `make rtt-max`（轮询在 OpenOCD 内 + 32 位分块读 + 12 KB 环） | **1140 KB/s** |
| **探针侧 RTT 桥**（`CMD_RTT` 0x31，固件自己轮询 RTT + CDC 转发） | **2190 KB/s（2.14 MB/s）** |

### 探针侧 RTT→CDC 桥（`feat/probe-rtt-bridge`）

把轮询从主机搬到探针固件里，是 J-Link 式 RTT 的做法（参考实现：
[MicroLink](https://github.com/minichao9901) 的同款 5301 工程）：固件自己在主循环里
读 RTT 控制块 → 搬环形缓冲 → 写回 RdOff，数据直接进 CDC 的 `g_uartrx` 环，主机只管
收串口。省掉的正是那 3 个 host↔探针来回。

- **SWD 访问直接用 ARM DAPLink 官方 `swd_host.c`**（见 `firmware/application_5301/src/swd_host/`），
  只裁掉 flash 算法/目标状态机部分，时序逻辑一字未改；平台胶水 `swd_host_port.c`
  把 `SWD_Transfer()` 分派到本工程 `SW_DP.c` 的 `SWD_Read()/SWD_Write()`，即与 DAP
  主机通路同一套按速度预编译的 bit-bang blob。
- **握手用默认低速档、之后再提速**（真实主机也是这个顺序）：`swd_init_debug()` 内部会
  再调一次 `swd_init()` → `DAP_Setup()` 把时钟重置回默认档，所以提速必须放在它之后。
- **背压 + 幂等重试保证不丢不重**：每轮只搬 `min(目标可读, 2048 B, CDC 环剩余空间)`；
  先交付到环再推进目标 RdOff，RdOff 写失败则记下来下轮补写（RdOff 是绝对值，重写无害）。
  实测 2×13.5 MB 全流校验 0 丢包 0 重包。
- 与 DAP 主机通路**互斥**：只在 DAP 空闲 ≥20 ms 时轮询，调试时最多多 ~1 ms 抖动。

启动方式（HID 自定义命令 `CMD_RTT` 0x31）：

```powershell
python script_test\rtt_probe_bridge.py COM52 6 36000   # 自动 boost 目标 + 启动桥 + 测速
```

吞吐随 SWD 时钟上升，但会撞到两侧不同的天花板（详见
[`docs/HPM5301EVKLite_port.md` §5.4](docs/HPM5301EVKLite_port.md#54-调优实测天花板在哪一侧2026-09-27)）：

| 量的是什么 | 数字 |
| --- | --- |
| SWD 侧（纯读目标 SRAM，最快 blob） | **3431 KB/s**（60 MHz 起 plateau，45 MHz 2844、36 MHz 2412） |
| 桥的搬运（丢弃模式，不送 CDC）@36 MHz | 2217 KB/s |
| 端到端（CDC 读走）@36 MHz / @45 MHz | 2207 / **2218 KB/s，零丢包** |

块大小 512 B → 2048 B 只多 1.2%（每块固定开销本来就只有 5 次传输）；
`clock_delay` 覆盖无差别；`__inline__` 无收益（`-O3` 已把
`swd_read_block`/`swd_transfer_retry`/`swd_read_word` 全部内联，符号表里已不存在）。

高频档是**间歇性**的（同样 36 MHz，长块读全对而控制块扫描可能失败），所以固件带
两处自愈：启动时从请求档位往下找可用档、运行中连续出错则降一档重来；控制块扫描
也从 4 字节小读改成 512 B 重叠窗块读（传输数少一个数量级）。扫描脚本
`script_test/rtt_bridge_sweep.py` 一次跑完「纯 SWD / 丢弃 / 端到端」三段对照。

另外：目标**运行中**时长块 SWD 读会失败（内核抢总线），必须限长分块 + 重试
（桥里默认 512 B/块）。详见
[`script_test/README.md`](script_test/README.md#rtt-测速为什么慢实测结论2026-09-27)。

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
