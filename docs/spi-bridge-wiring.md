# USB→SPI/QSPI 桥 接线速查（HPM5301EVKLite / J3 40pin）

> ★ = SPI 桥用到的脚　⛔ = 本工程占用或无引出，别接　○ = 空闲可挪作辅助脚
> 引脚表出处：官方 UG `docs/HPM5301EVKLite_UG_V1.1.pdf` 表 2（Rev1.1）。
> 辅助脚（DC/RST/BL/CS_AUX/TE）**不写死**，都可以用 HID `PIN_CFG` / `SET_CFG` 换到别的空闲脚。

## 1. 排针全貌（俯视，1/2 脚在 USB 那一端）

```
      奇数脚                                       偶数脚
  ┌──────────────────────────────────────────────────────────┐
  │  1 ● 3V3                              5V ●  2            │
  │  3 ● PB09  (I2C_SDA)                  5V ●  4            │
  │  5 ● PB08  (I2C_SCL)                 GND ●  6            │
  │  7 ● PA02  ○                    UART_TXD ●  8  PB15 ⛔    │
  │  9 ● GND                        UART_RXD ● 10  PB14 ⛔    │
  │ 11 ● PA31  ★ QSPI IO3                 NC ● 12            │
  │ 13 ● PB11  ★ DC                      GND ● 14            │
  │ 15 ● NC                               NC ● 16            │
  │ 17 ● 3V3                              NC ● 18            │
  │ 19 ● PA29  ★ MOSI / IO0              GND ● 20            │
  │ 21 ● PA28  ★ MISO / IO1               NC ● 22            │
  │ 23 ● PA27  ★ SCLK               SPI_CS0 ● 24  PA26 ★ CS   │
  │ 25 ● GND                        SPI_CS1 ● 26  PB10 ★ TE   │
  │ 27 ● PB12  ★ RST                     GPIO ● 28  PB13 ★ BL │
  │ 29 ● PY00  ⛔                         GND ● 30            │
  │ 31 ● PY01  ⛔                        GPIO ● 32  PA09 ⛔    │
  │ 33 ● PA10  ⛔(板载 LED)               GND ● 34            │
  │ 35 ● NC                     UART_LOG_TX ● 36  PA00 ⛔     │
  │ 37 ● PA30  ★ QSPI IO2       UART_LOG_RX ● 38  PA01 ⛔     │
  │ 39 ● GND                              NC ● 40            │
  └──────────────────────────────────────────────────────────┘
```

## 2. 逐脚用途表

| 脚 | 板载丝印/功能 | MCU | SPI 桥里的角色 |
|---|---|---|---|
| 1 / 17 | 3.3V | — | 给屏供电（VCC） |
| 2 / 4 | 5.0V | — | 一般不用（屏多是 3.3V） |
| 3 | I2C_SDA | PB09 | ○ 空闲 |
| 5 | I2C_SCL | PB08 | ○ 空闲 |
| **6 / 9 / 14 / 20 / 25 / 30 / 34 / 39** | **GND** | — | **地，必须接** |
| 7 | GPIO | PA02 | ○ 空闲（注意：悬空读出来是 0，不适合当"上拉默认高"的输入脚） |
| 8 / 10 | UART_TXD/RXD | PB15/PB14 | ⛔ 探针 VCOM（CDC 串口）占用 |
| **11** | GPIO | **PA31** | **★ QSPI IO3**（只在 quad 档用；开了 quad 就不能再当辅助脚） |
| **13** | GPIO | **PB11** | **★ DC**（命令/数据选择，默认脚） |
| 15 | NC | — | — |
| **19** | SPI_MOSI | **PA29** | **★ MOSI / IO0** |
| **21** | SPI_MISO | **PA28** | **★ MISO / IO1**（SPI+DC 屏不接 MISO 就空着） |
| **23** | SPI_SCLK | **PA27** | **★ SCLK** |
| **24** | SPI_CS0 | **PA26** | **★ CS** |
| **26** | SPI_CS1 | **PB10** | **★ TE 输入**（默认）／也可当备用 CS |
| **27** | GPIO | **PB12** | **★ RST**（低有效，默认脚） |
| **28** | GPIO | **PB13** | **★ BL**（背光，高=亮，默认脚） |
| 29 / 31 | GPIO | PY00/PY01 | ⛔ 属于 PIOC 域，固件 v1 不支持 |
| 32 | GPIO | PA09 | ⛔ 板载 USER/BOOT 按键脚 |
| 33 | GPIO | PA10 | ⛔ 板载 LED |
| 36 / 38 | UART_LOG | PA00/PA01 | ⛔ UART0 console / ROM ISP |
| **37** | GPIO | **PA30** | **★ QSPI IO2**（同上，quad 档用） |
| 12 / 16 / 18 / 22 / 35 / 40 | NC | — | — |

## 3. 三种接法

### A. 回环自测（不要屏，验固件的 P1/P2 验收项）

```
J3[19] MOSI ──跳线── J3[21] MISO        一根线，别的都不用接
```

跑：`python script_test/spi_bridge_test.py loop`（16 个长度 × 轮询/DMA）。
失败先跑 `pintest` —— 它会直接把"跳线没插好"和"控制器的问题"分开。

### B. 天马 2P01 / AXS15352（`profile = 1`，4 线 SPI + DC）

```
屏 VCC   ← J3[1]  或 J3[17]     3V3
屏 GND   ← J3[6]/[9]/[14]/[20]/[25]/[30]/[34]/[39]
屏 SCL   ← J3[23]  PA27         SCLK
屏 SDA   ← J3[19]  PA29         MOSI
屏 DC    ← J3[13]  PB11         DC        (pad_dc   = 1 = PB11)
屏 RST   ← J3[27]  PB12         RESET     (pad_rst  = 2 = PB12)
屏 BL    ← J3[28]  PB13         背光      (pad_bl   = 3 = PB13)
屏 TE    ← J3[26]  PB10         撕裂输入  (pad_te   = 4 = PB10，可选)
（这块屏没有 MISO，J3[21] 空着）
```

配置：`profile=1`、`dc_active_high=1`、`cs_policy=0`（PA26 自动 GPIO CS）、
`sclk_hz = 20~40 MHz`。初始化表一行 = 一个 `STEP` 帧。

### C. ST77916（`profile = 2`，QSPI 四线）

```
屏 VCC   ← J3[1]/[17] 3V3      屏 GND ← 任一 GND
屏 SCL   ← J3[23]  PA27   SCLK
屏 D0    ← J3[19]  PA29   MOSI / IO0
屏 D1    ← J3[21]  PA28   MISO / IO1
屏 D2    ← J3[37]  PA30   IO2
屏 D3    ← J3[11]  PA31   IO3
屏 CS    ← J3[24]  PA26   CS
屏 RST   ← J3[27]  PB12   RESET   (pad_rst = 2)
屏 BL    ← J3[28]  PB13   背光    (pad_bl  = 3；20 kHz PWM 调光 v1 不做)
屏 TE    ← J3[26]  PB10   TE      (pad_te  = 4，可选)
```

配置：`profile=2`、`qspi_wr_opcode=0x02`、`qspi_addr_bytes=3`、`sclk_hz=40 MHz`。
**注意**：开了 quad 之后 PA30/PA31 被 IO2/IO3 占用，不能再当辅助脚（固件会拒绝）。
刷像素：`XFER{cmd=0x32, addr_len=3, tcfg.lines=4, tx_len≤492}` 切片发。

## 4. 用逻辑分析仪抓包时的通道对照（当前接法）

| LA 通道 | 接 | 看什么 |
|---|---|---|
| CH0 | J3[13] PB11 | DC（命令/数据翻转，必须在 CS 窗口内） |
| CH1 | J3[27] PB12 | RST（复位脉冲宽度） |
| CH2 | J3[28] PB13 | BL（背光开关） |
| CH3 | J3[24] PA26 | CS（片选窗口，用来框住上面三根） |
| — | J3[20]/[25] GND | **GND 一定要接** |

想同时看 SCLK/MOSI 就得空出一个通道；回环抓包时用 `CH0=CS、CH1=MISO、CH2=MOSI、CH4=SCLK`
（注意 SCLK 我们接在 **CH4** 上，不是 CH3 —— 踩过一次）。

## 5. 辅助脚想换到别的脚

协议里的 pad 表（`SET_CFG` 的 pad 字段 / `PIN_CFG` 的 line+索引）：

| 索引 | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 引脚 | 无 | PB11 | PB12 | PB13 | PB10 | PA02 | PA09 | PA00 | PA01 | PY00 | PY01 | PA10 | PA30 | PA31 |
| J3 脚 | — | 13 | 27 | 28 | 26 | 7 | 32 | 36 | 38 | 29 | 31 | 33 | 37 | 11 |

`PIN_CFG` 的 line 号：**0=DC、1=RST、2=CS_AUX、3=BL、4=TE**（TE 是 4，别写成 5）。
换完记得 `ENABLE 0 → ENABLE 1` 让引脚重新配置。

> 实测提醒：这块板上 `PA02 / PA09 / PA00 / PA01` 悬空读出来是 **0**（内部上拉拉不起来），
> 不适合当"悬空=1"的输入脚；TE 用 **PB10** 时悬空读 1、短到 GND 读 0，行为是干净的。
