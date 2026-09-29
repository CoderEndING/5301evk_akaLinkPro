# USB → SPI/QSPI 转发功能方案（v1，待评审）

> 分支：`feature/usb-spi-bridge`　目标板：HPM5301EVKLite（探针）
> 状态：**方案阶段**，未写代码、未上板（用户当前在用探针）。评审通过后按 §8 分阶段实施。

---

## 0. 结论速览

| 议题 | 结论 | 依据 |
|---|---|---|
| **端点** | **新开一对 bulk**：IN `0x8B` / OUT `0x0B`（物理 EP11，双向同名），挂在新增的 vendor-specific 接口上；**不复用 CDC** | §3 |
| 外设 | `SPI1`；引脚 PA26/27/28/29 + **PA30/PA31（quad IO2/IO3）**，全部在 J3 排针上 | §2.1 §2.3 |
| 传输形态 | 单线 / 双线 / **四线**；硬件自带 cmd / addr / dummy / token 相位，一次 CS 内完成 | §2.2 |
| TX 策略 | `len < 100` 轮询；`len ≥ 100` DMA；**阈值可配**，上板实测后再定 | §6 |
| RX 策略 | 一律轮询（CPU 收 FIFO），与 TX 策略解耦 | §6 |
| 缓冲 | 32 KB AHB SRAM（`0xF0400000`，当前完全空闲）：OUT 环 16 KB + IN 环 8 KB + 暂存 2 KB + 预留 6 KB；DLM 只多花约 0.3 KB 元数据 | §5.2 |
| 控制面 | HID 新增 `CMD 0x35`（配置 / 状态 / 使能 / 复位 / 中止），沿用 0x31/0x32/0x34 的报文约定 | §4.4 |
| 数据面 | bulk OUT = **有序帧流**（数据 + CS/DC/延时等线序动作统一走这条）；bulk IN = 读数据 + 应答 | §4.2 §4.3 |
| 与现有功能 | **引脚零冲突**；主循环挂一个 `spi_bridge_poll()`，未使能时一条分支直接返回 | §2.9 §5.1 |
| 预期吞吐 | 单线 20 MHz ≈ 2.5 MB/s；四线 20 MHz ≈ 10 MB/s（USB HS 侧余量远大于此） | §6.3 |

---

## 1. 需求与边界

**需求**（用户 2026-09-28）

1. 新分支、新功能：USB → SPI/QSPI 转发。
2. 协议自定，上位机（web）按此实现。
3. 目标用途：驱动 LCD / QSPI 屏，以及各类 SPI 模块。
4. `len < 100` 轮询发送；`len > 100` DMA 发送；接收用轮询。
5. 端点：新开一对 bulk 还是复用 CDC —— 要给出结论。
6. 缓冲建议放 32 KB AHB SRAM。
7. 探针暂时不能用，先写代码，后上板验证。

**本方案不做**

- 网页侧实现（只给协议契约，后续单独出 handoff 文档）。
- 具体某块屏的初始化序列：由主机下发，固件保持通用（不内置面板型号表）。
- 专用命令集（NOR 的 sector erase 等）：用通用帧即可拼出。
- 与 JTAG/DAP/RTT/Scope 的功能耦合：SPI 桥是独立通道，互不干扰。

---

## 2. 硬件调研结论（附证据）

### 2.1 J3 排针完整引脚表

来源：`docs/HPM5301EVKLite_UG_V1.1.pdf` 表 2（官方用户手册，已本地提取核对）。

| 奇数脚 | 信号 | | 偶数脚 | 信号 |
|---|---|---|---|---|
| 1 | 3.3V | | 2 | 5.0V |
| 3 | PB09 | | 4 | 5.0V |
| 5 | PB08 | | 6 | GND |
| 7 | PA02 | | 8 | **PB15**（UART3.TXD，探针 VCOM 占用） |
| 9 | GND | | 10 | **PB14**（UART3.RXD，探针 VCOM 占用） |
| 11 | **PA31** | | 12 | NC |
| 13 | PB11 | | 14 | GND |
| 15 | NC | | 16 | NC |
| 17 | 3.3V | | 18 | NC |
| 19 | **PA29**（SPI1.MOSI） | | 20 | GND |
| 21 | **PA28**（SPI1.MISO） | | 22 | NC |
| 23 | **PA27**（SPI1.SCLK） | | 24 | **PA26**（SPI1.CS0） |
| 25 | GND | | 26 | PB10（板上标注 SPI_CS1，GPIO） |
| 27 | PB12 | | 28 | PB13 |
| 29 | PY00 | | 30 | GND |
| 31 | PY01 | | 32 | PA09 |
| 33 | PA10（板载 LED2） | | 34 | GND |
| 35 | NC | | 36 | PA00（UART0.TXD / log） |
| 37 | **PA30** | | 38 | PA01（UART0.RXD / log） |
| 39 | GND | | 40 | NC |

**SPI1 六线全部在排针上**（这是本功能成立的前提）：

| 功能 | 引脚 | J3 脚 | 说明 |
|---|---|---|---|
| CS0 | PA26 | 24 | 硬件 CS，`IOC_PA26_FUNC_CTL_SPI1_CS_0` |
| SCLK | PA27 | 23 | `IOC_PA27_FUNC_CTL_SPI1_SCLK` |
| MOSI / IO0 | PA29 | 19 | 单线时 MOSI；双/四线时 DAT0 |
| MISO / IO1 | PA28 | 21 | 单线时 MISO；双/四线时 DAT1 |
| **IO2** | **PA30** | **37** | `IOC_PA30_FUNC_CTL_SPI1_DAT2` |
| **IO3** | **PA31** | **11** | `IOC_PA31_FUNC_CTL_SPI1_DAT3` |

> SDK 的 `boards/hpm5301evklite/pinmux.c` 只初始化 PA26~PA29（`init_spi1_pins()`），**没有 quad 的 DAT2/DAT3**——需要我们在应用侧补一份引脚初始化。

### 2.2 SPI1 的硬件能力（够用且富余）

来源：`soc/HPM5300/HPM5301/hpm_soc_ip_feature.h`、`hpm_soc_feature.h`、`soc/HPM5300/ip/hpm_spi_regs.h`。

| 能力 | 值 | 对我们的意义 |
|---|---|---|
| `SPI_SOC_TRANSFER_COUNT_MAX` | `0xFFFFFFFF` | 单次传输长度不受 512 限制，**不需要联动描述符**（SDK 例程里那套 `dma_linked_descriptor` 在 HPM5301 上被 `#if` 排除） |
| `SPI_SOC_FIFO_DEPTH` | 8 项 | 轮询路径的粒度；配合 DataMerge 一次可灌 32 B |
| `HPM_IP_FEATURE_SPI_NEW_TRANS_COUNT` | 1 | 32 位传输计数 |
| `HPM_IP_FEATURE_SPI_CS_SELECT` | 1 | 硬件 CS0~CS3 可选（本板只有 CS0 引到排针） |
| `HPM_IP_FEATURE_SPI_SUPPORT_DIRECTIO` | 1 | 可用 DIRECTIO 寄存器直接驱动 CS/SCLK/IO（备用手动时序手段） |
| 数据相位格式 | `single / dual / quad` | **QSPI 屏的核心** |
| 地址相位格式 | `single / dualquad` | QSPI 屏的地址相位 |
| 相位 | cmd / addr / dummy(1~4) / token(0x00 或 0x69) | 覆盖 QSPI 屏与 SPI NOR/传感器的常规时序 |
| 传输模式 | write / read / write_read / dummy_read / … | 覆盖双向场景 |
| 时序旋钮 | `cpol/cpha`、采样沿、`cs2sclk`、`csht` | CS 与时钟相对时序可调（屏类器件敏感） |

驱动 API（`drivers/inc/hpm_spi_drv.h`，全部可直接用）：

- `spi_master_timing_init()` / `spi_format_init()` / `spi_control_init()`
- **轮询整帧**：`spi_transfer(ptr, cfg, &cmd, &addr, wbuf, wlen, rbuf, rlen)` —— 一次调用完成 cmd+addr+数据相位
- **DMA**：`spi_setup_dma_transfer(ptr, cfg, &cmd, &addr, wcount, rcount)` —— 先 `spi_control_init` 配好相位，再由 DMA 喂数据相位（cmd/addr 由驱动写寄存器完成，**同一次 CS 窗口内**）
- `spi_wait_for_idle_status()` 收尾、`spi_directio_*()` 兜底

参考例程：`samples/drivers/spi/master_trans_large_amount_of_data`（DMA + cmd/addr + GPIO CS 的完整范式）、`samples/drivers/spi/polling/master`、`samples/spi_components/*`。

### 2.3 QSPI（四线）可行性 —— 关键结论

- HPM5301 的 SPI1 有 `DAT2 = PA30`、`DAT3 = PA31` 两处引脚功能（`soc/HPM5300/HPM5301/hpm_iomux.h`），**且这两个脚都在 J3 排针上**（J3[37] / J3[11]）。
- 驱动层 `spi_data_phase_format_t { single, dual, quad }`、`spi_addr_phase_format_t { single, dualquad }` 齐全。
- 结论：**能驱动真正的 QSPI 屏**（cmd 单线 + addr 四线 + data 四线，或 cmd/addr/data 全四线，按屏的时序配）。这正好对上「LCD/QSPI 屏」的需求。

### 2.4 板载 QSPI NOR 不占排针（不会打架）

- 板载 1 MB NOR 走 `XPI0`（`BOARD_APP_XPI_NOR_XPI_BASE = HPM_XPI0`），HPM5301 只有 XPI0 一个 XPI 控制器。
- XPI0 的 CA 总线可映射到两处：`PX00~PX07`（XPI 专用 pad）与 `PA24~PA31`。**板载 NOR 在 PX 上**——否则 SDK 自带的 SPI1 例程（用 PA26~PA29）会破坏 flash 连接，而它在 EVKLite 上是能跑通的；且 I2S-over-SPI 例程把 PA31 当普通 CS 用（`BOARD_I2S_SPI_CS_GPIO_PAD = IOC_PAD_PA31`）。
- 因此结论：**排针上的 PA26~PA31 可以放心当 SPI1 用**，与启动 flash 无冲突。XPI0 无法再拿来做第二路 QSPI 主机（唯一实例），所以「QSPI 屏」只能走 SPI1 的 quad 模式。

### 2.5 AHB SRAM：32 KB，当前完全空闲

| 项 | 值 | 证据 |
|---|---|---|
| 基址 / 大小 | `0xF0400000` / 32 KB | `soc/HPM5300/HPM5301/toolchains/gcc/flash.ld` 的 `MEMORY` |
| 应用 linker | `.ahb_sram (NOLOAD) : { KEEP(*(.ahb_sram)) } > AHB_SRAM` | `firmware/application_5301/linker/flash_dfu_app.ld:216-219` |
| 当前占用 | **0**（无任何 `.ahb_sram` 变量） | 全仓库 grep |
| 时钟/门控 | 无 sysctl 资源位，恒可访问 | `soc/HPM5300/HPM5301/hpm_sysctl_drv.h` 无 ahb_sram 资源 |
| NOLOAD 的性质 | 不进固件镜像、不参与 CRC/长度校验 | linker 段属性 |

**无 D-Cache**：HPM5301 没有 L1C 实例（`soc/HPM5300/ip/` 下无 `hpm_l1c_regs.h`，SoC 头文件里无 `HPM_L1C`）。所以：

- DMA 缓冲**不需要 cache 维护**（对比 SDK 例程里 `ATTR_ALIGN(HPM_L1C_CACHELINE_SIZE)` 那套是为带 cache 的型号准备的）；
- AHB SRAM 直接就是 DMA 可达内存（无需 `core_local_mem_to_sys_address()` 地址转换，那是 DLM/ILM 才需要的）。

### 2.6 USB 端点预算：够，而且加端点**不涨 RAM**

| 项 | 值 | 证据 |
|---|---|---|
| 硬件端点总数 | EP0~EP15（`ENDPTCTRL[16]`） | `soc/HPM5300/ip/hpm_usb_regs.h:49` |
| DCD 上限 | `USB_SOC_DCD_MAX_ENDPOINT_COUNT = 16` | `hpm_soc_feature.h` |
| 现用 EP | IN：1(DAP) 3(SWO/scope) 4(CDC) 6(CDC-INT) 7(HID) 9(MSC)；OUT：2(DAP) 5(CDC) 8(HID) 10(MSC) | `usb_composite.h` + `usb_composite.c` |
| 空闲 EP 索引 | **11 ~ 15** | 同上 |
| cherryusb 端点表 | `tx_msg[16]` / `rx_msg[16]` / `intf[16]` | `core/usbd_core.c:82-87` |
| DCD 数据竞技场 | `_dcd_data` 4096 B，**按 16 EP × 双向静态分配** | 上一轮 DLM A 档瘦身的实测 |
| → 新增一对 EP 的 RAM 代价 | **0 B**（QHD/QTD 已按最大端点数为每对端点预留） | 同上 |

蓝本：J-Scope 采样器用的 bulk IN `0x83`（`swo_in_ep` + `scope_sampler_tx_complete()` 的「在飞包账本」流控），新通道照抄这套模式即可。

### 2.7 DMA：`dma_mgr` 自动分配，不与现有功能抢通道

- 当前只用 2 个通道：CDC UART 的 RX/TX（`HPM_DMA_SRC_UART3_RX/TX`），由 `dma_mgr_request_resource()` 动态申请（`src/usb/cdc_interface.c`）。
- SPI1 的请求源已就绪：`HPM_DMA_SRC_SPI1_RX = 0x2A`、`HPM_DMA_SRC_SPI1_TX = 0x2B`（SDK `board.h` 里就有 `BOARD_APP_SPI_TX_DMA`）。
- 通道数充裕（`DMA_SOC_CHANNEL_NUM = 32`），且用 `dma_mgr` 申请可避免硬编码冲突。

### 2.8 引脚占用与冲突检查

| 资源 | 探针现用 | 与 SPI 桥 |
|---|---|---|
| DAP（J5 调试口） | PA04 TDO / PA05 TDI / PA06 TCK / PA07 TMS / PA08 nRESET | 无冲突 |
| VCOM（UART3） | PB15 TXD / PB14 RXD（J3[8]/[10]） | 无冲突 |
| 板载 LED | PA10（J3[33]，低有效） | 无冲突（**但别选它当辅助 GPIO**） |
| USER KEY | PA03（不在排针） | 无冲突 |
| VREF ADC | PB10 **仅 akaLinkPro 有**；EVKLite `BOARD_HAS_VREF_ADC = 0` → **PB10 空闲** | 可用作辅助 GPIO（板上标注 SPI_CS1） |
| SPI1 | 未使用 | 本功能占用 PA26~PA31 |

**辅助 GPIO 候选（J3 上、EVKLite 构建下空闲）**：PB10(J3[26])、PB11(J3[13])、PB12(J3[27])、PB13(J3[28])、PA02(J3[7])、PA09(J3[32])、PA00/PA01(J3[36]/[38])、PY00/PY01(J3[29]/[31])。

**默认建议接线**（QSPI 屏，全在 J3）：

```
屏 SCLK  ← J3[23] PA27       屏 CS   ← J3[24] PA26
屏 D0    ← J3[19] PA29       屏 D1   ← J3[21] PA28
屏 D2    ← J3[37] PA30       屏 D3   ← J3[11] PA31
屏 DC    ← J3[13] PB11       屏 RESET← J3[27] PB12
屏 BL    ← J3[28] PB13       GND ← J3[6/9/14/20/25/30/34/39]，3V3 ← J3[1/17]
```

这几个辅助脚**不做死**：协议里由主机按「pad 表索引」配置，固件校验合法性（例如开了 quad 就不允许把 PA30/PA31 再配成辅助 GPIO）。

---

## 3. 端点决策：新开一对 bulk（推荐），不复用 CDC

| 维度 | 新开 bulk 对（0x8B/0x0B） | 复用 CDC（0x84/0x05） |
|---|---|---|
| 隔离性 | **独占**，与 COM 口 / RTT-over-USB 互不影响 | 与 VCOM、RTT 桥**共用同一条字节流**，需要额外复用/分帧 |
| 与 0x34 桥开关的耦合 | 无 | CDC 桥被 `CMD_BRIDGE` 关掉时 SPI 桥一起断 |
| 报文边界 | 512 B 整包，天然有边界 | 纯字节流，帧边界靠协议自己找 |
| 主循环影响 | 独立回调 + 独立环 | 要和 UART 环、RTT 抢同一条 IN 队列 |
| RAM 代价 | **0 B**（§2.6） | 0 B |
| 描述符改动 | 新增 1 个接口 + 2 个端点（`INTF_NUM`/`USB_CONFIG_SIZE` 是宏，改一处即可） | 无 |
| 主机侧复杂度 | 打开一个接口、两条管道，语义清晰 | 需要与 RTT/VCOM 复用同一条管道，网页侧已经有一套时序假设 |
| 风险 | 配置描述符改动需要回归一次枚举（VID/PID 不变，接口号后移） | 可能干扰现有 RTT/COM 的稳定性 |

**结论：新开一对 bulk。** 代价（一次描述符改动 + 一次枚举回归）远小于复用 CDC 带来的协议耦合与回归面。CDC 端的 VCOM/RTT 是探针现有主力功能，不该被新功能共享管道。

**接口与端点分配**

| 项 | 取值 |
|---|---|
| 新接口号 | HID 之后插入（`SPI_INTF_NUM = HID_INTF_NUM + CONFIG_CHERRYDAP_USE_CUSTOM_HID`），WebUSB/DFU 接口号宏随之顺移 |
| bInterfaceClass | `0xFF`（vendor specific），`bNumEndpoints = 2` |
| OUT 端点 | `0x0B`（EP11 OUT），bulk，512 B（HS） |
| IN 端点 | `0x8B`（EP11 IN），bulk，512 B（HS） |
| 字符串 | 复用现有 iInterface 习惯，可留 0 |

---

## 4. 协议设计

### 4.1 分层

```
       ┌───────────── HID 0x35（控制面，64 B 报文）─────────────┐
       │ 配置（SCLK/模式/线数/CS 策略/DMA 阈值/辅助脚）、状态与统计、  │
       │ 使能/失能、复位、中止                                    │
       └───────────────────────────────────────────────────────┘
       ┌──────── bulk OUT 0x0B（有序帧流）┬ bulk IN 0x8B（应答流）┐
       │ 数据 + 一切与**线序**有关的动作：  │ 读数据 + 每个 RSP 帧的   │
       │ 传输帧、CS 拉低/释放、DC/RESET、  │ 应答（含 seq / 状态）；   │
       │ 延时、保活                       │ 另可异步上报错误事件      │
       └─────────────────────────────────┴───────────────────────┘
```

**为什么线序动作必须走 bulk**：USB 两条管道之间没有顺序保证。如果「CS 拉低」走 HID、「数据」走 bulk，两者的到达顺序无法界定。把参与时序的动作全部放进同一条 bulk OUT 流，探针按 FIFO 顺序执行，顺序性由协议本身保证。

**为什么数据不塞进 HID**：HID 报文只有 64 B，且与 RTT/Scope/RISCV 共用；大块像素数据必须走 bulk。

### 4.2 bulk OUT 帧格式（主机 → 探针）

约束：**一个 USB 包（≤512 B）内可含多帧，但一帧不跨包**。这样每帧的 payload 在物理上连续，DMA 可以直接从环里取，不需要额外搬运。

```
帧头（8 B，小端）
  0: u16 magic = 0x4253      ('S','B')
  2: u8  type                帧类型
  3: u8  flags               标志
  4: u16 seq                 主机分配，应答原样带回（用于请求/应答配对）
  6: u16 len                 本帧 payload 字节数
```

**flags**

| 位 | 名称 | 含义 |
|---|---|---|
| 0 | `RSP` | 要求回一个应答包（bulk IN） |
| 1 | `CS_HOLD` | 本帧结束后**保持 CS 有效**（跨越多次传输的器件） |
| 2 | `CS_OFF` | 本帧结束后释放 CS |
| 3 | `CS_AUX` | 本次用**辅助 CS 线**（默认用硬件 CS0） |
| 4 | `NO_DMA` | 本帧强制走轮询（对照/调试） |
| 5 | `FORCE_DMA` | 本帧强制走 DMA |
| 6-7 | 保留 | 必须为 0 |

**type**

| 值 | 名称 | payload |
|---|---|---|
| 0x01 | `XFER` | 12 B 传输头（见下）+ TX 数据 |
| 0x02 | `CS` | `u8`：0 = 释放，1 = 拉低（手动 CS，配合 `CS_HOLD`） |
| 0x03 | `GPIO` | `u8 line, u8 level`；line：0=DC，1=RESET，2=AUX_CS，3=BL |
| 0x04 | `DELAY` | `u32` 微秒（屏初始化用） |
| 0x05 | `PING` | 空；用来测往返延迟 / 保活（配 `RSP`） |
| 0x06 | `CFG` | 数据面内改运行参数（可选，v1 只支持改 `tx_dma_threshold`） |

**XFER 的 12 B 传输头**

```
 0: u8  cmd          命令字节（tcfg.cmd_en = 1 时有效）
 1: u8  tcfg         位域：
                       bit1:0 数据相位线数  0=1线 1=2线 2=4线
                       bit2   cmd_en        发送命令相位
                       bit3   addr_en       发送地址相位
                       bit4   addr_quad     地址相位用 2/4 线（否则单线）
                       bit5   dc_en         传输前先设 DC 线
                       bit6   dc_level      DC 电平（1 = 数据，0 = 命令）
                       bit7   token_en      发送 0x69 token（部分屏需要）
 2: u8  addr_len     地址字节数 0~4
 3: u8  dummy        dummy 周期 0=无，1~4
 4: u16 tx_len       数据相位发送字节数
 6: u16 rx_len       数据相位接收字节数
 8: u32 addr         地址（小端；addr_len 决定实际发几个字节，MSB 优先）
12: ...              tx_len 字节的发送数据
```

一次 `XFER` = **一次硬件 SPI 事务 = 一次 CS 窗口**，时序为：

```
CS↓ ─ [cmd] ─ [addr] ─ [dummy] ─ [data(tx/rx, 1/2/4 线)] ─ CS↑
```

（`CS_HOLD` 时末尾不抬 CS，留给后续帧。）

`tx_len` / `rx_len` 的分工与 SDK 的 `spi_trans_mode_t` 对应：都非 0 → `write_read`；只有 tx → `write_only`；只有 rx → `read_only`。

**长度上限**：`len ≤ 512 - 8 = 504`；`tx_len ≤ len - 12`，`rx_len ≤ 504`（收到 IN 包时按 `len` 回读）。

### 4.3 bulk IN 应答格式（探针 → 主机）

```
 0: u16 magic = 0x4253
 2: u8  type     0x81 = RSP（对 RSP 帧的应答）；0x82 = EVT（异步事件/错误）
 3: u8  status   0=OK 1=未使能 2=帧格式错 3=参数越界 4=SPI 超时
                 5=IN 环满（丢弃） 6=DMA 错误 7=设备忙
 4: u16 seq      对应帧的 seq（EVT 时为最后处理的帧）
 6: u16 len      后面跟的读数据字节数（≤ 504）
 8: ...          len 字节读数据
```

- 只有带 `RSP` 的帧才产生应答 → 纯写入流不产生任何 IN 流量，主机可以全速灌数据。
- 帧本身不带 `RSP` 但执行出错时，探针发 `EVT`（同时计数进 HID 统计），主机在空闲时读取即可。

### 4.4 HID 控制面：新增 `CMD 0x35`（SPI 桥）

沿用现有约定（`src/api/api_param.c`）：

- 请求：`req[2] = 0x35`，`req[3] = action`，`req[4..]` = 参数
- 响应：`res[2] = 0x35`，`res[3] = action` 回显，`res[4..7]` = 32 位状态字，`res[8..]` = 附加数据

| action | 名称 | 请求 | 响应 |
|---|---|---|---|
| 0 | `STATUS` | — | `res[4..7]` 状态字（见下）+ `res[8..]` 计数器（见下） |
| 1 | `ENABLE` | `req[4]`：0 关 / 1 开 | `res[4..7]` = 当前开关状态 |
| 2 | `RESET` | — | 清环、复位状态机、清计数器（不动配置） |
| 3 | `SET_CFG` | `req[4..]` = 配置块（≤56 B） | `res[4..7]` = 状态；非法字段被夹取并在 `res[8]` 列出 |
| 4 | `GET_CFG` | — | `res[4..]` = 配置块 |
| 5 | `PIN_CFG` | `req[4]`=line，`req[5]`=pad 索引，`req[6]`=有效电平 | 非法映射返回错误码（例如要 quad 却把 PA30 配成 GPIO） |
| 6 | `ABORT` | — | 丢弃未处理帧与 IN 队列，回到干净状态 |

> 说明：CS/GPIO 的**直接**操作**不放** HID，避免与 bulk 流产生顺序歧义；HID 只做配置与状态（§4.1）。

**状态字 `res[4..7]`（u32 小端）**

| 位 | 含义 |
|---|---|
| 0 | 已使能 |
| 1 | 正在处理帧 |
| 2 | CS 当前有效 |
| 3 | IN 侧被流控暂停（等主机取走数据） |
| 4 | OUT 环接近满 |
| 8-15 | 最近一次错误码 |
| 16-31 | 预留 |

**计数器 `res[8..]`**（8 × u32，小端）：`frames_ok`、`frames_err`、`bytes_tx`、`bytes_rx`、`tx_poll_cnt`、`tx_dma_cnt`、`out_ring_overrun`、`in_ring_drop`。

### 4.5 配置块（SET_CFG / GET_CFG）

```
 0: u32 sclk_hz          期望 SCLK（0 = 板级默认 20 MHz）
 4: u8  mode             SPI 模式 0~3（CPOL/CPHA）
 5: u8  bits             数据位宽（v1 固定 8）
 6: u8  cs_policy        0 = 硬件 CS0 每帧自动；1 = 辅助 CS 每帧自动；2 = 手动
 7: u8  tx_dma_threshold 默认 100；0 = 全轮询；0xFF = 全 DMA
 8: u8  pad_dc           辅助脚 pad 索引（0 = 不用）
 9: u8  pad_rst
10: u8  pad_cs_aux
11: u8  pad_bl
12: u8  pad_active_low   位图：bit0 DC、bit1 RST、bit2 CS、bit3 BL
13: u8  flags            bit0 = 随 ENABLE 一起打开；bit1 = 使能时自动清环
14: u16 out_ring_kb      请求的 OUT 环大小（受 32 KB 总预算夹取）
16: u16 in_ring_kb
18: u16 max_frame_bytes  v1 固定 504
20: ...                  预留（配置块总长 32 B，留出余量）
```

**pad 索引表**（协议内固定，避免主机猜 IOC 编号）：

| 索引 | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 引脚 | 无 | PB11 | PB12 | PB13 | PB10 | PA02 | PA09 | PA00 | PA01 | PY00 | PY01 | PA10 | PA30 | PA31 |
| J3 脚 | — | 13 | 27 | 28 | 26 | 7 | 32 | 36 | 38 | 29 | 31 | 33 | 37 | 11 |

> - PA30/PA31 只有在**没开 quad**时才允许当辅助脚；固件在 `PIN_CFG`/`SET_CFG` 里校验并拒绝。
> - PA09 是板载 TinyUF2 按键脚（按下 + 复位进 DFU），PA10 是板载 LED——两者列在表里只为完整性，**默认不选**。
> - PA00/PA01 是 UART0（ROM ISP / log）——ISP 场景下会被外部串口驱动，作为辅助脚时需自行留意。

### 4.6 典型时序示例

**A. QSPI 屏写命令（cmd 单线 + 24 位地址 + 四线数据）**

```
OUT: [XFER RSP] seq=1 tcfg{cmd_en,addr_en,lines=4} cmd=0x02 addr_len=3 addr=0x00002C tx_len=2 rx_len=0
     payload: 0x12 0x34            ← 屏侧：CS↓ 0x02 A[23:0] 四线 2 B CS↑
IN : [RSP] seq=1 status=0
```

**B. QSPI 屏读（带 dummy）**

```
OUT: [XFER RSP] seq=2 tcfg{cmd_en,addr_en,lines=4,dummy} cmd=0x03 addr_len=3 addr=0x000004 dummy=2 rx_len=4
IN : [RSP] seq=2 status=0 payload: 4 B 读值
```

**C. 经典 SPI 屏（ST7789 类，DC 控制）**

```
OUT: [XFER] seq=3 flags=RSP tcfg{dc_en, dc_level=0} cmd=0 tx_len=1  payload: 0x2A   ← DC=0 写命令
OUT: [XFER] seq=4 flags=RSP tcfg{dc_en, dc_level=1} cmd=0 tx_len=4  payload: 00 00 01 3F
```

**D. 批量刷图（150 KB 像素，主机切片）**

```
每片：XFER seq++ tcfg{cmd_en=1,addr_en=1,lines=4} cmd=0x02 addr=RAMWR tx_len=480 payload=480 B
（只有最后一片带 RSP，用来确认整批完成；中间片不带 RSP，不产生 IN 流量）
```

**E. SPI 传感器读寄存器（BMI270 类）**

```
OUT: [XFER RSP] tcfg{cmd_en=1} cmd=0x80|reg tx_len=0 rx_len=1 dummy=1
```

**F. SPI NOR（W25Q 类）**

```
读： cmd=0x03 addr_len=3 addr=offset rx_len=N
写： cmd=0x02 addr_len=3 addr=offset tx_len=N
```

---

## 5. 固件架构

### 5.1 模块与挂载点

新增文件（`firmware/application_5301/src/spi_bridge/`）：

| 文件 | 职责 |
|---|---|
| `spi_bridge_proto.h` | **协议唯一真源**：帧头、type、flags、传输头、HID action、pad 表、错误码 |
| `spi_bridge.c/.h` | 状态机、帧解析、传输执行（轮询/DMA）、统计；导出 `spi_bridge_init/poll/enable/...` |
| `spi_bridge_usb.c` | bulk IN/OUT 端点回调 + 环读写 + 流控（照抄 scope 的「在飞账本」模式） |

改动的既有文件：

| 文件 | 改动 |
|---|---|
| `src/usb/usb_composite.h` | 新增 `SPI_IN_EP 0x8B` / `SPI_OUT_EP 0x0B` 宏 |
| `src/usb/usb_composite.c` | 描述符加接口 + 2 端点；`INTF_NUM`/`USB_CONFIG_SIZE`/接口号宏顺移；`usbd_add_endpoint`；IN/OUT 回调；复位时清环 |
| `src/api/api_param.c` | 新增 `CMD_SPI 0x35` 分支（动作见 §4.4） |
| `src/main.c` | 主循环加 `spi_bridge_poll();` |
| `boards/hpm5301evklite/pinmux.c` | 新增 `init_spi1_pins_quad()`：PA26~PA29 + 可选 PA30/PA31；**不要**照抄 SDK 的 `LOOP_BACK_MASK` 位（那是回环测试用的） |
| `boards/hpm5301evklite/board.h` | `BOARD_SPI_BRIDGE_*` 默认参数（SCLK、辅助脚、阈值） |
| `firmware/application_5301/CMakeLists.txt` | 新增源文件 |

主循环（`src/main.c`）：

```c
while (1) {
    chry_dap_handle();
    if (usb2uart_bridge_enabled) chry_dap_usb2uart_handle();
    api_param_poll();
    dfu_key_poll();
    rtt_bridge_poll();
    scope_sampler_poll();
    riscv_svc_poll();
    spi_bridge_poll();      /* 未使能 → 一条分支返回；使能 → 处理若干帧后返回 */
}
```

**延迟预算**：`spi_bridge_poll()` 每次最多处理有限的帧/字节（例如「≤ 4 帧或 ≤ 2 KB，先到为准」），避免单次占用主循环过久影响 DAP 与 Scope。剩余帧下一轮继续——OUT 环足够大，不会因此丢数据。

**未使能时的代价**：一个 `if (!enabled) return;`。bulk OUT 端点也**不再重新 arm**（主机侧写入会 NAK，天然背压），所以完全闲置时不产生任何中断负载。

### 5.2 AHB SRAM 分区

| 区域 | 大小 | 用途 |
|---|---|---|
| OUT 环 | 16 KB | 32 × 512 B 槽；帧按包存放，payload 天然连续 → DMA 源可直接指进环 |
| IN 环 | 8 KB | 16 × 512 B 槽；应答包排队 |
| 线性暂存 | 2 KB | 预留（跨槽拼接、非连续 DMA 源、需要字节重排时用；v1 一般不用） |
| 预留 | 6 KB | 以后放大帧 / 双缓冲 / 第二设备 |
| **合计** | **32 KB** | |

放在 `.ahb_sram`（NOLOAD，不进镜像、不影响 DFU 长度与 CRC）。DLM 侧只花元数据：`len[32] + state[32]`（OUT）、`len[16] + state[16]`（IN）、配置与统计 ≈ **300 B 上下**（现 DLM 106592/130304 B = 81.8%，余量充足）。

### 5.3 关键实现要点

1. **引脚复用只在使能时做**：默认（未使能）固件行为完全不变，不会影响 DAP/VCOM/RTT/Scope。
2. **每帧一次事务**：轮询用 `spi_transfer()`；DMA 用 `spi_setup_dma_transfer()` + `dma_mgr` 申请的 `HPM_DMA_SRC_SPI1_TX` 通道，等 `dma_check_transfer_status()` 的 TC 后 `spi_wait_for_idle_status()` 再收尾 CS。
3. **CS 策略**：默认硬件 CS0 每帧自动；`CS_HOLD`/`CS_OFF`/`CS` 帧支持手动跨帧保持；`cs_policy = 1` 时改用辅助 GPIO 当 CS（多器件）。
4. **RX 轮询**：`spi_transfer()` 内部逐次读 RX FIFO（8 深）；全双工时驱动已按「写一次、收一次」交错，不会溢出（`drivers/src/hpm_spi_drv.c:211`）。
5. **DMA 阈值判定**：看 `tx_len`（数据相位发送字节数），不看整帧长度。
6. **`data_merge`**：v1 用 0（与 SDK 阻塞 API 语义一致，字节精确）；若上板实测轮询路径成瓶颈，再考虑改用 DataMerge（8-bit 下每次 DATA 读写 4 字节，FIFO 一项抵 4 字节）配合自写 FIFO 循环。
7. **超时**：每帧都有硬超时（`spi_wait_for_idle_status` 重试上限），超时 → 状态回 4、计数器 +1、必要时复位 SPI 控制器（`spi_reset_all`），绝不挂死主循环。
8. **不做 cache 维护**（无 D-Cache），也不需要地址转换（AHB SRAM 是系统地址）。

---

## 6. 发送/接收策略与阈值

### 6.1 TX：`< 100` 轮询，`≥ 100` DMA

- **轮询**（`spi_transfer()`）：CPU 逐字节/逐字灌 TX FIFO。小帧时省掉 DMA 通道配置与完成等待，路径最短、无中断。
- **DMA**（`spi_setup_dma_transfer()`）：CPU 只做配置 + 等 TC。大帧时 CPU 占用从「与线速同步」降为 O(1)，主循环能继续照看 USB/DAP。
- 阈值 `tx_dma_threshold` 默认 **100 B**（用户指定），**可配、可单帧覆盖**（`NO_DMA`/`FORCE_DMA`）。
- 粗算依据（360 MHz CPU、8 项 FIFO、`data_merge=0`）：
  - DMA 侧固定开销：通道配置 + 启动 + 等 TC 轮询 ≈ 0.5~1 µs；
  - 轮询侧边际开销：约 3~8 周期/字节 ⇒ 100 B ≈ 1~2 µs；
  - 交叉点大致落在 100~200 B 区间，**与用户给的 100 一致**。
  - 注意：`data_merge=1` 能让轮询边际开销降到 ~1/4，交叉点会右移到 400 B 上下——所以这个阈值最终要靠上板实测（§8 的对照测试），我先按 100 实现。

### 6.2 RX：一律轮询

- 主机侧读长度已知（`rx_len`），CPU 收 FIFO 即可，无需 DMA 通道与描述符。
- 读数据直接写进 IN 槽 → 立刻排队发走，无二次搬运。
- 例外：将来若要支持「超长连续读」（> 504 B 的 D 类场景），可考虑 RX DMA —— 属于 v2 议题。

### 6.3 吞吐预估（不含主机与协议开销）

| 配置 | 线速上限 | 150 KB 整屏（240×320×2 B）耗时 |
|---|---|---|
| 单线 20 MHz | 2.5 MB/s | ≈ 60 ms |
| 单线 40 MHz | 5 MB/s | ≈ 30 ms |
| **四线 20 MHz** | **10 MB/s** | **≈ 15 ms** |
| 四线 40 MHz | 20 MB/s | ≈ 7.5 ms |

- USB 侧：HS bulk 512 B/包。探针已有两条 bulk 通道的实测：J-Scope 采样器的 bulk IN `0x83` 在满速测试里跑到 **26.1 kHz × 512 B ≈ 13 MB/s** 的包速率，RTT-over-CDC 跑到 **1.39 MB/s（且是被 SWD 侧限速的结果）**。而 SPI 转发没有 SWD 那样的逐包处理开销，**USB 侧余量远大于 SPI 线速**，瓶颈在 SPI。
- 帧间空隙（协议解析 + 事务启动 ≈ 1~2 µs）相对 480 B/帧（四线 20 MHz ≈ 48 µs）只有几个百分点，**v1 不做流水线**；若以后需要，可让连续写帧在硬件 CS 自动控制下背靠背下发。
- SCLK 上限待上板实测（与 SPI1 时钟源、排针走线、从器件能力有关）。协议里 `sclk_hz` 可调，先按 20 MHz 起步，逐步往上试。

---

## 7. 与网页侧的接口约定（契约要点）

1. 打开 `VID 0x0D28 / PID 0x0204` 的**新接口**（vendor specific，2 个 bulk 端点）。
2. 控制面走现有自定义 HID：`req[2] = 0x35`，动作见 §4.4；响应形状与 0x31/0x32/0x34 一致。
3. 数据面：bulk OUT 发帧（§4.2），bulk IN 收应答（§4.3）。
4. 使用顺序建议：`GET_CFG` → `SET_CFG`（SCLK/线数/CS 策略/辅助脚/阈值）→ `ENABLE 1` → 灌帧 → 需要时 `ABORT`/`RESET` → `ENABLE 0`。
5. 主机必须自己切片（单帧 ≤ 504 B），并自己维护 `seq` 配对。
6. 批量写建议「只有最后一片带 `RSP`」，避免 IN 流量拖慢灌数据。

---

## 8. 实施阶段与验收

| 阶段 | 内容 | 上板验收标准 |
|---|---|---|
| **P1** | 协议头文件 + HID 0x35（配置/状态/使能/复位）+ SPI1 引脚与初始化 + 帧解析 + **轮询 TX/RX** + AHB SRAM 环 + 端点描述符 | 枚举后能看到新接口；`GET_CFG` 回读正确；**MISO↔MOSI 跳线回环**：1/2/32/99/100 B 收发逐字节一致 |
| **P2** | **DMA TX**（≥ 阈值）+ 阈值可配 + 统计计数 | 同上回环在 101/256/504/484 B 通过；对比 `NO_DMA`/`FORCE_DMA` 两种路径的耗时，定阈值默认值 |
| **P3** | 辅助 GPIO（DC/RESET/CS/BL）+ 手动 CS（`CS_HOLD`/`CS_OFF`/`CS` 帧）+ `DELAY` + 多器件（辅助 CS） | 万用表/逻辑分析仪看 DC/RESET 电平与 CS 波形；两块 SPI 从机（或屏 + 传感器）分别选中互不干扰 |
| **P4** | 真器件：QSPI 屏（或 SPI NOR 作对照）+ 四线读回（如 `0x03`/`RDDID`） | 四线读到的 ID 与器件手册一致；整屏刷图时间与 §6.3 预估同量级 |
| **P5** | 文档：`docs/web-handoff-spi-bridge.md`（给网页组的实现说明）+ README 更新 + DLM/Flash 占用复核 | 文档自洽；DLM 与 Flash 数字更新 |

**P1/P2 的关键验收手段是回环**（J3[19] MOSI ↔ J3[21] MISO 一根跳线）：
- 不依赖任何外部器件，能覆盖「轮询 vs DMA」「单线/双线」「CS/DC 时序」和协议解析；
- 与之前 UART 回环（`script_test/uart_loopback.py`）同一套路，便于回归；
- 新脚本：`script_test/spi_bridge_test.py`（WebUSB/裸 HID 均可），跑长度扫描 + 阈值对照 + 统计校验。

---

## 9. 风险与待确认

### 9.1 需要用户拍板

1. **端点方案**：按 §3 新开一对 bulk（0x8B/0x0B）——是否同意？
2. **辅助脚默认值**：DC=PB11(J3[13])、RESET=PB12(J3[27])、BL=PB13(J3[28])、备用 CS=PB10(J3[26])——是否按这个默认？（协议里可改，改默认只是省事。）
3. **SCLK 起步值**：默认 20 MHz（板级既有值），上板后往上试；有没有指定目标（例如 40/60/80 MHz）？
4. **显示屏型号**：有没有已经确定的屏（型号/接口线数/DC 有无）？会影响 P4 的验证顺序（但**不影响协议**）。若屏是「无 DC、纯 QSPI 命令序列」型，示例 C 可以完全不用。

### 9.2 技术风险

| 风险 | 影响 | 缓解 |
|---|---|---|
| 描述符改动导致枚举回归（接口号顺移） | DAP/CDC/HID/WebUSB/DFU 全部要复测 | 接口追加在 HID 之后、宏统一顺移；改动后跑一遍现有 `script_test/` 全套 |
| 主循环被长帧拖住 | DAP/Scope 时序抖动 | 每轮帧数/字节上限；DMA 处理大帧；未使能时零开销 |
| 四线时序与从器件不匹配（dummy/CS 时序） | QSPI 屏读不到数据 | `cpol/cpha/cs2sclk/csht/dummy/addr_quad` 全部可配；先用 SPI NOR 或回环验证四线链路 |
| IN 环被未取走的数据填满 | 卡住帧处理 | 流控暂停 + 硬超时 + 计数器上报；文档里强调主机要取走 IN |
| 辅助脚与排针上其它用户（J3 共用）冲突 | 误驱动外部电路 | pad 表白名单 + 使能时才配置 + `PIN_CFG` 校验 |
| SCLK 提升后信号完整性 | 数据错误 | 起步 20 MHz；提升靠实测；必要时降 `cs2sclk` 或加串阻 |
| 探针暂不可用 | 无法即时验证 | 本阶段只写代码与文档；所有数值（阈值、SCLK 上限）标注为待实测 |

---

## 10. 下一步

1. 用户确认 §9.1 的四个点（端点方案最关键）。
2. 我按 P1 → P4 顺序实现，每个阶段在分支上独立提交（中文 commit）。
3. 有板子后：构建（`cmd /c build_dfu_evklite.bat`）→ DFU 烧写（`hpm6800_flash_probe.py --no-build`）→ 跑 `script_test/spi_bridge_test.py` → 出验收数据 → 更新文档与 README。
