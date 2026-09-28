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

**目标侧**支持两类调试通路，同一份固件按需切换：

- **ARM（SWD 为主，也支持 JTAG）** —— 主力通路，按速度预编译的 bit-bang blob。
- **RISC-V（JTAG-only）** —— RISC-V Debug Module（DMI + SBA）+ 探针侧搬运引擎，
  见 [HPM6800EVK（HPM6880，RISC-V）目标调试](#hpm6800evkhpm6880risc-v目标调试)。

## 主要特性

- **CMSIS-DAP 调试器**：USB-HS 复合设备（DAP + CDC + 自定义 HID + WebUSB + DFU Runtime），
  SWJ 支持 SWD/JTAG；bit-bang 引擎按速度预编译（20/30/36/45/60 MHz 档 + Slow C 版），
  实测目标 SRAM 读在 60 MHz 档达 2376~2640 KB/s。
- **探针侧 SEGGER RTT→CDC 桥**（HID `CMD_RTT` 0x31）：把 RTT 轮询从主机下沉进探针固件
  （J-Link 式），主机只读一个串口。**默认 45 MHz 档：2527 KB/s（2.47 MB/s）零丢包**；
  切 60 MHz 档：**2954 KB/s（2.89 MB/s）** —— 比主机轮询上限（1140 KB/s）快 **2.6 倍**。
  详见 [探针侧 RTT→CDC 桥](#探针侧-rttcdc-桥)。
- **支持 RISC-V 目标（JTAG-only，HID `CMD_RISCV` 0x33）**：新增探针侧 RISC-V
  Debug Module 引擎（DMI + SBA，`src/riscv/` + 专用 DMI 扫描汇编，TDI 预置 + 循环
  展开）。调 HPM6800EVK（HPM6880）实测 SRAM 读 **1504.5 KB/s**、写 **1511.8 KB/s**，
  比主机驱动 OpenOCD 快 **9 倍**；RTT 交付 **1385 KB/s 且字节级零丢包**（28.5 MB
  不丢不重），有效 TCK 20.7 MHz（目标规格上限 25 MHz）。
  详见 [HPM6800EVK（HPM6880，RISC-V）目标调试](#hpm6800evkhpm6880risc-v目标调试)。
- **DFU/MSC Bootloader**：长按 USER 键进 DFU，虚拟 U 盘 `AKALINKPRO` 拖入 `.bin` 即升级；
  APP 带签名 + 长度 + CRC32 校验，校验失败停在 DFU。
- **配置持久化 + WebHID 上位机**：配置存 QSPI NOR（EasyFlash），`docs/index.html` 可直接改。
- **两块硬件一套源码**：akaLinkPro 原板与 HPM5301EVKLite 移植板，靠 `board.h` 特性宏切换。

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

探针侧 RTT 桥的三个脚本（需要目标板跑 `script_test/stm32f103_rtt_speed`，默认自带 96 MHz 超频）：

```bat
python script_test\rtt_probe_bridge.py COM52 10       :: 桥测速 + 全流零丢包校验（最常用）
python script_test\rtt_rate_matrix.py COM52           :: 逐档对照：SWD 读速 / 交付率 / 占比 / 谁主导
python script_test\rtt_bridge_sweep.py COM52 --clk=60 :: 调优扫描：时钟 x 块大小 x 丢弃模式
```

### SRAM 吞吐：主机驱动 vs 纯 SWD 链路

这两个数字**不是一回事**，放一起看才不会被误导：

**① 纯 SWD 链路天花板**（探针内部基准：`CMD_RTT` action 8，读目标 SRAM，
不经 USB、不经主机、不含 RTT 描述符开销）—— 这才是"链路本身能跑多快"：

| SWD 档 | 20 MHz | 30 MHz | 36 MHz | 45 MHz | **60 MHz** |
| --- | --- | --- | --- | --- | --- |
| 纯 SWD 读 | 1469 | 2041 | 2351 | 2766 | **3281 KB/s（3.2 MB/s）** |
| RTT 交付 | 1379 | 1882 | 2171 | 2485 | **2932 KB/s** |

复现：`python script_test\rtt_rate_matrix.py COM52 --sec 5`
（同一张表里还给出交付/读的占比与"谁主导"的判定）。
60/80/100 MHz 都落在同一个 60M blob 上，所以 60 MHz 起就 plateau 了。

**② 主机驱动**（`make sram-test`：OpenOCD `load_image`/`dump_image` 走 CMSIS-DAP
USB 批量端点）—— 比链路天花板低一档，但**不是因为"每趟往返开销"**：OpenOCD 用
4 深 pending FIFO + 异步 URB 已经把每包的固定开销藏掉了，实测把 CMSIS-DAP 包大小
从 512 提到 1024（固件现在按 2×mps 报 `DAP_Info(PKT_SZ)`，块读从 127 ops/包变成
254 ops/包）速度**完全不变**——证据见 `docs/experiment-dap-resp-size.md`。
⇒ 这条路的真正上限是**探针的 SWD 位翻转率**（同档 in-probe 读 3281 KB/s）叠加 USB
搬运，不是往返次数。数字本身受主机栈与目标主频双重影响
（下表为 **目标 STM32F103 @96 MHz** 实测，复现误差 ±1~10%）：

| SWD 时钟 | 写 | 读 |
| --- | --- | --- |
| 1 MHz | 85.6 KB/s | 85.8 KB/s |
| 2 MHz | 176.6 KB/s | 175.4 KB/s |
| 4 MHz | 339.9 KB/s | 336.5 KB/s |
| 10 MHz | 797.6 KB/s | 766.0 KB/s |
| 20 MHz | 1355.9 KB/s | 1287.3 KB/s |
| 36 MHz | 2117.5 KB/s | 1924.8 KB/s |
| 45 MHz | 2420.4 KB/s | 2180.8 KB/s |
| 60 MHz | 2843.9 KB/s | 2443.0 KB/s |

> 这两个口径的差别（60 MHz 档：墙钟读 2.5 MB/s、纯传输读 2.9 MB/s，链路天花板
> 3.3 MB/s），正是"链路天花板 3.3 MB/s"与"主机驱动 2.4 MB/s"看起来矛盾的原因：
> 表①量的是探针把 SWD 数据搬进自己内存的速度，表②量的是主机经 USB 把数据取走
> 的速度 —— 探针同一颗 CPU 要**串行地**干这两件事（一条条处理 DAP 命令），所以
> 两者耗时基本相加。想拿满链路带宽，就得像探针侧 RTT 桥那样**把轮询下沉进固件**
> （见下一节）。

> **口径提醒**：上表是**墙钟**计时（`sram_speed_test.py` 自己掐表），每条
> `load_image`/`dump_image` 都要经一次 telnet 往返（实测 ~1 ms/条），在 20 KB 的
> 量级上占掉约 19%。OpenOCD 自己打印的**纯传输计时**（`dumped N bytes in X s
> (Y KiB/s)`，原作者 `script_test/swd/benchmark_readback.tcl` 用的就是这个口径、
> 64 KB × 10 轮）在同一档位是 **读 2941 / 写 3386 KB/s** ⇒ 读已到 in-probe 链路
> 天花板的 **90%**。两个口径都对，引用时要说清是哪个。

> **`adapter speed` 是档位选择器，不是实际频率**：固件 `Set_Clock_Delay()` 把它
> 映射到 6 个固定引擎（60/45/36/30/20 MHz 的 ASM blob，其余走 `swd_speed_calc()`
> 的延时环）。所以"请求 20 MHz"实际得到的是 20M blob（本机实测读 1469 KB/s），
> **不是** 20 MHz 的精确链路。另有 `clock_accel_mode`（HID `CMD_SET_CONFIG`
> byte[5]，默认关）会把请求值 **×10** —— 于是"请求 20 MHz"会被顶到 60M 引擎，
> 这正是原作者截图里"20 MHz 却跑到 3.6 MB/s"的原因（物理上 20 MHz SWD 的写上限
> 只有 ~1.74 MB/s）。本机用**他本人的脚本**、在 **F103ZET6（64 KB，与他同样的块长）**
> 上复现：60M 引擎下 **写 3471 / 读 2958**，与他 3646/3045 只差 **4.8% / 2.9%**；
> 而字面 `adapter speed 20000` 只有 1487/1400 —— 截图那个数不可能来自 20M 引擎。
> 完整复现方法（含脚本搜索路径的坑）与对照表见 **`docs/upstream-speed-claims.md`**，
> 该文档也记录了 F103ZET6 狂发例程（`build.ps1 -Board ze`，RTT 上行 32 KB，
> 实测交付 2486 KB/s 无损）。

### 2026-09-28 收口：瓶颈在**探针侧**，不在目标侧

三个目标、两套取数工具（本探针 vs SEGGER J-Link）跑下来，结论收敛了。

**RTT 交付（探针侧 RTT→CDC 桥，同一套脚本 `rtt_probe_bridge.py` / `rtt_h743_bridge.py`）：**

| SWD 档 | **H743 @480 MHz** | F103 @96 MHz | SEGGER J-Link @50 MHz |
| --- | --- | --- | --- |
| 20 MHz | 1379.6 | 1379 | — |
| 36 MHz | 2162.3 | 2162 | — |
| 45 MHz | 2486.6 | 2484 ~ 2487 | — |
| **60 MHz** | **2931.2 KB/s** | **2932 ~ 2954 KB/s** | 1473.0 KB/s |
| （对照）H743 复位默认 64 MHz 时 | ~700 KB/s，**曲线是平的** ⇒ 那时是目标受限 | — | 692.9 |

**SRAM 主机驱动（64 KB，60000 档，OpenOCD 自带计时/纯传输口径，逐字节校验通过）：**

| 平台 | 写 | 读 |
| --- | --- | --- |
| **H743 @480 MHz** | **3430** | **2689** |
| H743 @64 MHz（复位默认） | 2590 | 2764 |
| F103ZET6 @96 MHz | 3471 | 2958 |
| （对照）原作者 ZET6 截图 | 3645.959 | 3045.310 |

**四条结论：**

1. **两个完全不同的目标撞在同一个数字上**（RTT 交付 2931 vs 2934，差 0.1%；SRAM 写
   3430 vs 3471，差 1.2%）⇒ **瓶颈已经是探针自己的 USB/CPU 吞吐（~2.9 MB/s），
   目标侧不再是瓶颈**。想再快只能动探针（USB 吞吐 / 位翻转引擎）。
2. **目标主频确实重要，但 400 MHz 以上就饱和**：H743 从 64 → 480 MHz，RTT 交付
   693 → 1473（J-Link 口径）；400 与 480 **完全一样**（1472.7 vs 1473.0）。
   ⇒ H743 固件现在**上电自动升到 480 MHz**（`script_test/stm32h743_rtt_speed`，
   照抄厂商 `Stm32_Clock_Init` 序列；坑见源码注释与 §6）。
3. **探针比 J-Link 快一倍**（同目标同主频：2931 vs 1473），J-Link 的 CLI 取数更低
   （692）⇒ SEGGER 那套数字**不能**当"目标侧天花板"的参照，它先撞自己的上限。
4. **SWD 时钟是唯一还线性有效的旋钮**：探针口径下 20→60 MHz 交付 1379→2931 一路上涨
   （60 MHz 偶有长跑抖动，默认仍保守用 45 MHz，见 `rtt_rate_matrix.py`）。

> `script_test/stm32f103_rtt_speed` 固件自己就超频到 **96 MHz**（HSE 8 MHz ×12，
> `main.c` 的 `clock_init()`），所以上面的数字是 96 MHz 目标的实测值；更早的
> 64 MHz 基线（60 MHz 档写 2640 / 读 2376 KB/s）略低 —— 提目标主频只换来约 8%，
> 说明这条通路的瓶颈**不在目标侧**，而在探针（SWD 位翻转 + USB 搬运）。
>
> ⚠️ 这个数字还受**目标机主频**限制（每次 SWD AHB-AP 事务要花几个目标 HCLK）：
> STM32F103 上电默认 HSI 8 MHz 时，无论 SWD 时钟拉到多高都会卡在 ~1.4 MB/s，
> 而 F1 的 `reset halt` 是核心级复位、不清 RCC，所以数字会随目标上电后的状态
> 变化一倍（`sram_speed_test.py` 会打印实测时钟，`--no-boost` 可关闭它的补偿；
> 它现在还会识别"固件已跑在 PLL 上"从而不去动 RCC）。
>

### 2026-09-28 追加：RISC-V 走 JTAG —— 主机侧是**往返受限**，探针侧才有速度

第一次用本探针调 **RISC-V**（HPM6800EVK / HPM6880，只有 JTAG、没有 SWD）。这一块
内容已经长成独立一章 —— 接线与前置条件、**必须烧 ELF 的启动头坑**、引擎与 RTT
交付率、TCK 频率上限分析、复现清单，全部见下面的
**[HPM6800EVK（HPM6880，RISC-V）目标调试](#hpm6800evkhpm6880risc-v目标调试)**，
细节在 [`docs/hpm6800evk-jtag.md`](docs/hpm6800evk-jtag.md)。

一句话结论：主机驱动下每个 abstract command 要一个 USB 往返（97 µs），读速
95.9 KB/s；把搬运下沉进探针固件后 **1504.5 KB/s（9 倍）**，RTT 交付
**1385 KB/s 且字节级零丢包**（28.5 MB，0 丢 0 重）。

### SEGGER RTT 吞吐：从 919 KB/s 到 2.9 MB/s

RTT 是**主机轮询**模型：每次取数要 3 个 host↔探针来回（读 WrOff/RdOff → 读环形
缓冲 → 写回 RdOff），而 SRAM 测速是一次大块流水传输，所以两者不可比。实测阶梯
（目标均归一到 64 MHz）：

| 配置 | 吞吐 |
| --- | --- |
| 目标停在复位默认 8 MHz（RTT 生产者在目标侧，此时封顶） | 277 KB/s |
| `make rtt-test`（OpenOCD rtt server） | **919 KB/s** |
| `make rtt-max`（轮询在 OpenOCD 内 + 32 位分块读 + 12 KB 环） | **1140 KB/s** |
| **探针侧 RTT 桥**（`CMD_RTT` 0x31，固件自己轮询 RTT + CDC 转发） | **2527 KB/s（2.47 MB/s）零丢包**；切到 60 MHz 档可到 **2954 KB/s（2.89 MB/s）** |

### 探针侧 RTT→CDC 桥

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
| SWD 侧（纯读目标 SRAM） | 20/30/36/45/60 MHz → 1476/2053/2359/2788/**3312** KB/s |
| 桥的搬运（丢弃模式，不送 CDC）@60 MHz | 3232 KB/s |
| 端到端（CDC 读走）@45 MHz（默认）/ @60 MHz | **2527** / **2954 KB/s，零丢包** |

60 MHz 档原来做完整初始化会失败（换挡瞬态），已用「斜坡换挡 + 换挡后热身 + 失败先清
sticky 错误再判死」修好；但它**长跑偶尔抖动**（约每 5~10 次 10 秒一次），所以默认仍取
稳定的 45 MHz，想要极限速度可用 HID `CMD_RTT` action 7 切 60 MHz（固件带自动降档兜底）。
现在 60 MHz 下的瓶颈已转到 USB/CDC（丢弃 3232 vs 端到端 2954），详见
[`docs/HPM5301EVKLite_port.md` §5.8/§5.9](docs/HPM5301EVKLite_port.md#58-swd-读速-vs-rtt-交付率逐档对照表)。

> ⚠️ 测交付率时主机侧读法影响极大：Windows 上 pyserial 的 `ser.read(n)` 会把主机侧压到
> 2169 KB/s，`ser.readinto(大缓冲)` 才有 2956 KB/s（差 36%）。所有脚本已改用 readinto。

块大小 512 B → 2048 B 多 1.2%（每块固定开销本来就只有 5 次传输）；
`clock_delay` 覆盖无差别；`__inline__` 无收益（`-O3` 已把
`swd_read_block`/`swd_transfer_retry`/`swd_read_word` 全部内联，符号表里已不存在）。

高频档是**间歇性**的，所以固件带三处自愈：启动时从请求档位往下找可用档
（60→45→36→30→20→10）、运行中连续出错则降一档重来（只在重扫也失败时降，否则一次瞬态
就会白白降档）、换挡后先读一次 DP IDCODE 热身并清 sticky 错误；控制块扫描
也从 4 字节小读改成 512 B 重叠窗块读（传输数少一个数量级）。扫描脚本
`script_test/rtt_bridge_sweep.py` 一次跑完「纯 SWD / 丢弃 / 端到端」三段对照。

另外：目标**运行中**时长块 SWD 读会失败（内核抢总线），必须限长分块 + 重试
（桥里默认 512 B/块）。详见
[`script_test/README.md`](script_test/README.md#rtt-测速为什么慢实测结论2026-09-27)。

脚本说明见 [`script_test/README.md`](script_test/README.md)；`sram/rtt` 脚本的工具路径
可用 `OPENOCD_EXE`、`OPENOCD_SCRIPTS`、`HPM_SDK_ENV_DIR` 覆盖。

## HPM6800EVK（HPM6880，RISC-V）目标调试

第一块用本探针调的 **RISC-V** 目标，也是第一次走 **JTAG-only** 通路（HPM6880 没有
SWD）。它的调试模块是标准的 **RISC-V Debug Module**（DMI + SBA），跟 ARM 的
DAP/AHB-AP 完全不是一回事，所以固件里新增了一整套 `src/riscv/`。

> 完整记录（三个 DTM 时序坑、失败实验的原始读数、逐步复现清单）在
> **[`docs/hpm6800evk-jtag.md`](docs/hpm6800evk-jtag.md)**，本节只放结论。

### 接线与前置条件

| 信号 | 探针（HPM5301EVKLite J5） | HPM6800EVK |
| --- | --- | --- |
| TCK | PA06 / J5.9 | JTAG TCK |
| TMS | PA07 / J5.7 | JTAG TMS |
| TDI | PA05 / J5.5 | JTAG TDI |
| TDO | PA04 / J5.13 | JTAG TDO |
| GND | J5.4·6·8… | GND |

三条必须知道的前提：

1. **探针要先切到 SWD+JTAG 模式**：`python script_test\hpm6800_probe.py set-mode 1`
   （HID `CMD_SET_CONFIG` 的 `output_mode`；0 = SWD+VCOM 会**拒绝 JTAG**）。
   这个设置**只存在 RAM 里，探针一复位/重插就丢**，每次上电都要重设。
2. **排线第 15 脚是探针自己的 `RESET_N`**：`openocd_hpm6800evk_dap.cfg` 里必须
   `reset_config none`，让复位走 DM 的 `ndmreset`。否则 OpenOCD 一复位**把探针自己
   打掉**（实测掉过两次，靠给上游 USB Hub 断电才救回来）。
3. **`adapter speed` 对 JTAG 扫描完全无效** —— JTAG 汇编把 delay 写死传 0，
   改它没有任何效果（真正的旋钮见下面的 TCK 一节）。

### 烧录：**必须烧 ELF，SDK 生成的 `.bin` 里没有启动头**

这是最容易白掉半天的一条。SDK 输出的 `.bin` 里 **`.boot_header` 整段是全 0**，
ROM 认不出来，复位后 PC 停在 boot ROM `0x2001d4c8` 一动不动 —— 看起来像"BOOT 跳线
配错了"，其实跳线 `BOOT0=0 / BOOT1=0` 本来就是对的（NOR 启动）：

| 文件 | `0x80001000`（启动头） |
| --- | --- |
| `demo.elf` 的 `.boot_header` | `bf109000…`（tag `0x009010BF`，正常） |
| 它导出的 `demo.bin` | **全 0** ← 烧这个就不启动 |

```bat
python script_test\hpm6800_flash_target.py     :: 默认就是狂发固件 ELF，并打印复位后 PC 自检
```

烧完的自检三件套：`mdw 0x80001000` = `009010bf`、复位后 `pc` = `0x80003000`、
`mdw 0x1240000` 读到 `"SEGGER RTT"`。

### 速度：主机驱动 vs 探针侧引擎（**9 倍差距**）

| 路径 | 写 | 读 |
| --- | --- | --- |
| OpenOCD `progbuf` 后端（三个后端里最好） | 154.0 KB/s | 95.9 KB/s |
| OpenOCD `sba` 后端 | 87.0 | 85.3 |
| OpenOCD `abstract` 后端 | 14.5 | 14.4 |
| **探针侧 DMI/SBA 引擎**（新增 `src/riscv/`） | **1511.8 KB/s** | **1504.5 KB/s** |

差 9 倍的原因是**往返**：主机驱动下每个 abstract command（最多 4 个字）就要一次
USB 往返（实测 **97 µs**），而 40 µs/字的读速正好等于这个往返 —— 测出来就是
"一个往返换一个字"；`sba`/`abstract` 后端更差。**只有把搬运下沉进探针固件才有速度。**

探针侧的做法：加载一次 `IR = 0x11`（DMI）之后，**一次 DMI 访问 = 一次 41 位 DR 扫描**
（`{op[1:0], data[31:0], addr[6:0]}`），而且响应**滞后一拍**，所以连续的 posted 请求可以
一个字一次扫描地流水，没有任何往返。块搬运走 Debug Module 的 **SBA**（系统总线访问，
硬件自增地址）。

> 关于"上游是不是有个没用上的优化汇编"：**没有**。`JTAG_Sequence()` 一直在调
> `JTAG_Sequence_GPIO_ASM_45M`，且与上游 `akkako/akaLinkPro` **逐指令相同**（只差
> 引脚参数化）。真正的差距是 SWD 那套是 **6 周期/bit**，JTAG 这份是 **19 周期/bit**。
> 本次另写了专用 DMI 扫描汇编 `JTAG_DP_GPIO_ASM_DMI.S`（一次访问收进一个函数、
> 41 位请求在寄存器里移位），把探针侧从 954 一路做到 **1504/1512 KB/s**（见下一节）。

### RTT 交付率：**1385 KB/s，字节级零丢包**

狂发固件在 `script_test/hpm6800evk_rtt_flood/`（`flash_xip`，RTT 上行 32 KB，
`BLOCK_IF_FIFO_FULL`，死循环发 `hello world!\n`），控制块 `_SEGGER_RTT` 在
**0x01240000**（AXI SRAM，探针可直接读写）。

```bat
python script_test\hpm6800_rtt_delivery.py COM5 5     :: 交付率
python script_test\hpm6800_rtt_loss.py COM5 20        :: 字节流丢包校验
```

```
host read 7208960 bytes in 5.08s -> 1385.0 KB/s
bridge: drained=7211008 bytes, polls=3521, moves=3521, rderr=0, wderr=0

host received   28516352 bytes in 20.046s -> 1389.2 KB/s
probe drained   28516352 bytes (polls=13924 moves=13924 rderr=0 wderr=0 zips=0)
probe-host delta: 0 bytes
records=2193565 lost=0 dup=0
```

狂发固件写的是**固定 13 字节记录**，丢一个字节模式必然错位 —— 所以
"2193565 条完整记录、0 丢 0 重、probe-host delta 恰好 0"比计数器更能说明问题：
**28.5 MB 一个字节不差**。

> ⚠️ **测交付率时主机侧读法同样决定结果**：pyserial 的 `ser.read(n)` 每次新分配
> 缓冲，实测把主机侧压到 ~2169 KB/s；交付率一高就变成"主机侧丢字节"的假象
> （探针 `rderr/wderr` 全是 0，流里却少几个字节）。本项目所有测速脚本一律用
> `readinto()` + 复用同一个 1 MB 缓冲，并且**先停桥再收尾巴** —— 生产者停了以后
> 还缺的才算真丢包。

> 还踩过一个**跨后端适配层**的坑，很有代表性：`rtt_write_word()` 的两个后端
> 成功/失败方向相反（`swd_write_word()` 1 = 成功，`riscv_jtag_write_word()` 0 = 成功），
> RISC-V 分支忘了取反 ⇒ **回写 RdOff 成功被判成失败**，桥搬完第一块 2048 B 就永久
> 卡在"幂等补写"分支里打转，连控制块都不再读。它不报错、不崩，只表现为"速率是 0"。
> 定位过程（靠 `s_write_err` 在涨、`s_rd_pend_v` 却是 0 这对矛盾读数）见文档 §5.3。

### TCK 频率：从 16.4 MHz 提到 20.7 MHz，规格上限 25 MHz

一次 DMI 访问 = `idle(8) + 导航(5) + 移位(41)` = **54 TCK**，所以吞吐直接由"每 bit
多少拍"决定：

| 版本 | 每 bit 结构 | 读 | 有效 TCK |
| --- | --- | --- | --- |
| 初版专用汇编 | 低相位 8 条指令专等 TDI 建立 + 高相位 8 拍 nop 等 TDO | 1189.5 KB/s | 16.4 MHz（上限的 66%） |
| **TDI 预置 + 循环展开**（现行） | 低相位只剩 `sw DO_CLR`，TDI 提前到上一位的高相位摆好 | **1504.5 KB/s** | **20.7 MHz（83%）** |

关键点是：**TCK 已经是高的时候，把 `DO_VAL` 写成 `TCK|TDI` 只是改 TDI 电平、
不产生任何边沿**。所以"给下一位摆 TDI"可以提前到上一位的高相位去做，低相位就
退化成一条 store，只受目标最小 TCK 低电平宽度约束（实测 4 拍），不再受 TDI 建立
时间约束（那正是旧结构低相位那 8 拍的来源）。再把整个 32 位位移循环展开，每条位
又省下 `addi`+`bnez` 两拍。

按 54 TCK/字算，25 MHz 下的理论上限是 **1.85 MB/s**，当前 1.5 MB/s 是它的 81%。
剩下的空间在**导航相位**（13 个导航/idle 时钟 × 每个约 10 拍）：用同一招把 TMS 也
提前摆好，理论上还能再要百分之十几。

四个时序旋钮**都已经在最小值上**（每一点都用自检判死活 —— `hpm6800_selfcheck.py`
写已知图案再读回比对校验和，PASS 才算数）：

| 旋钮 | 现值 | 上界判据 |
| --- | --- | --- |
| `DMI_NAV_LOW_NOP` | 6 | **4 → FAIL，读回全 0**（TMS 建立不够） |
| `DMI_NAV_HIGH_NOP` | 4 | 4 通过（配合 `NAV_LOW=8` 时的 4/4 曾 FAIL，说明卡的是 LOW） |
| `DMI_TCK_LOW_NOP` | 4 | **2 → FAIL，校验和 `0x11D9A168`**（TCK 低电平太窄） |
| `DMI_CAP_HIGH_NOP` | 1 | **0 → FAIL**（采样点不够；TDI 预置后已有 8 条指令垫底，补 1 拍正好） |
| `idle`（运行时 `hpm6800_riscv.py delay <n>`） | 8 | **6 只跑得动 6/50 轮就死**；≤4 立刻不应答 |

> ⚠️ 改这段汇编时最坑的一条：45M 汇编里 `andi t4, a4, JTDI_OFFSET` 看着能把
> "取位 + 移位"合成一条，但那只在 **TDI 流已经预先左移过 `JTDI_SHIFT`** 的代码里
> 成立。套到这里（`\in` 是原始请求寄存器、bit0 才是当前位）会去取
> `bit[JTDI_SHIFT]`，于是请求被整串移成 0（`op=NOP`）—— **基准照跑、计数器全干净、
> idcode/dtmcs 也还是对的，只有自检能抓到读回全 0**。这就是自检必须当门禁的原因。

### 复现清单

```bat
:: 1) 探针：编译 + DFU 升级 + 切 JTAG 模式
cd firmware\application_5301
python ..\..\script_test\hpm6800_flash_probe.py
python ..\..\script_test\hpm6800_probe.py set-mode 1

:: 2) 目标：烧狂发固件（**必须 ELF**）
python script_test\hpm6800_flash_target.py

:: 3) 引擎自检与基准
python script_test\hpm6800_riscv.py open
python script_test\hpm6800_selfcheck.py                         :: PASS 才算数
python script_test\hpm6800_riscv.py rbench 0x1200000 1024 50
python script_test\hpm6800_riscv.py wbench 0x1200000 1024 50    :: 见下面 ⚠
python script_test\sram_speed_hpm6800.py --size 65536 --regions axi   :: 主机侧口径

:: 4) RTT 交付率
python script_test\hpm6800_rtt_delivery.py COM5 5
python script_test\hpm6800_rtt_loss.py COM5 10
python script_test\hpm6800_rtt_diag.py COM5 3                   :: 出问题时读探针 RAM 定位
python script_test\hpm6800_cdc_check.py COM5                    :: 拆 CDC 那一跳

:: 5) 时序扫描（每点一次构建 + 烧写 + 自检，约 2 分钟）
powershell -File script_test\hpm6800_timing_sweep.ps1
```

> ⚠️ **写基准的地址就是目标自己的 RAM**：`0x1200000` 是狂发固件 `.bss` 的起点，
> `wbench` 会把目标正在用的变量整片覆盖，目标随后就不产数据了（现象是 RTT 桥
> poll 几万次全是空环、交付塌到 3 KB/s，而**读回校验和仍然是对的**，只有速率会
> 暴露它）。做完写基准确认要么换空闲 scratch 地址，要么重烧一次目标。

HID 侧接口：`CMD_RISCV`（**0x33** —— 原本是 0x32，因网页侧 SCOPE 占了 0x32 而让位；动作见
[`Custom HID Protocol.md`](firmware/application_5301/Custom%20HID%20Protocol.md)）；
RTT 桥切目标类型用 `CMD_RTT`（0x31）的 action 10。

## J-Scope 波形（探针侧 HSS 采样）

类 SEGGER J-Scope 的**变量示波器**：探针自己按固定周期用 SWD 读目标 RAM 里 1~8 个变量，
组 512 B 自描述包，从 interface 0 上那个**原本闲置的 bulk IN `0x83`**（SWO 端点，
`SWO_STREAM=0` 所以一直没人写过）推给主机。**目标固件一行都不用改** —— 变量地址来自
目标 `.elf` 的 DWARF。上位机是另一仓库
[web-serial-rtt-tools](https://github.com/minichao9901) 的「J-Scope 波形」页（WebUSB）。

控制面 HID `CMD_SCOPE 0x32`（形状照抄 0x31），数据面 `0x83`。协议与状态字见
[`Custom HID Protocol.md`](firmware/application_5301/Custom%20HID%20Protocol.md) 第 16 条。

### 实测（8 通道 f32/u32/u16/u8 混排，64 字节一帧 → 22 B/样本）

| 口径 | 结果 |
| --- | --- |
| **M0 标定 @45 MHz**（一个 span、24 B） | 13.49 µs/样本 → **74.2 kHz** |
| **M0 标定 @60 MHz** | 11.55 µs/样本 → **86.5 kHz** |
| 端到端 25 kHz 采集 | **99.7% 交付**（丢 268/78258，全是启动瞬态） |
| 端到端 50 kHz 采集 | 1.10 MB/s，96.8% 交付（**宿主读速限制**，见下） |

### 三个必须知道的实测结论

1. **靶子的主频决定一切，不是探针。** AHB-AP 每次读都要花目标侧几个 HCLK：同一份靶子固件
   跑 HSI 8 MHz 时块读封顶 **1.47 MB/s（0.68 µs/字节）**，而且 45 MHz 与 30 MHz 的读数
   **一模一样**（目标已饱和）；改成 HSE ×12 = **96 MHz** 后同一路径是 **3.37 MB/s
   （0.297 µs/字节）**，SWD 时钟才重新变成线性有效的旋钮。拿 8 MHz 的靶子量采样率，
   量到的是靶子的上限（≈46 kHz）。→ 项目里那份靶子固件已经改成 96 MHz：
   [`script_test/stm32f103_scope/`](script_test/stm32f103_scope)。
2. **热路径是"一次采样 = 按 span 块读"**：变量按地址排序，间隙 ≤ 阈值就并成一个 span。
   一次 span 块读的传输数是 **N+2**（TAR + prime + (N-1)×DRW + RDBUFF），这已是 AHB-AP
   的理论最小 —— `swd_host.c` 里 CSW 与 DP_SELECT 都带缓存，第二次起写 CSW 是**零传输**。
   真正能省的只有搬运：span 内变量首尾相接时，**span 的字节序与帧内布局逐字节相同**，
   于是零拷贝直读进包（有填充字节的结构体不能直读 —— 帧内紧凑排会把 `u_hi` 放到 18
   而它在内存里是 20，硬直读会写错位置，这个坑已经踩过并写进代码注释）。
3. **包缓冲的账不能"拿不到就退回 0 号"**。那样会往**在飞**的缓冲里写数据，再对它
   `usbd_ep_start_write` 会因端点忙而**静默不启动**（既不发也不回调），于是
   `s_if_count` 只增不减、缓冲永远还不回来 —— 实测 `txDone=912/1136` 而"无缓冲丢样本"
   却等于每包一次，打包率掉到 1/4 还丢 seq。现在拿不到就丢拍并如实计数，等回调还回来。

> ⚠️ **测交付率时主机侧读法同样决定结果**：`usb.read(N)` 会一直等到凑满 N 字节才返回，
> 读 16 KB = 32 个包 = 14 ms，这期间探针的包缓冲早被填满并开始丢拍 —— 看起来像固件丢数据。
> 另外**读线程必须在 START 之前起来**（启动后那段没人读的时间的账会全记在 dropped 上，
> 实测 25 kHz 下 dropped=3192 ≈ 120 ms × 25 kHz − 8 个缓冲）。同一个坑在 RTT 交付率
> 脚本里已经踩过一次，见 [`docs/hpm6800evk-jtag.md`](docs/hpm6800evk-jtag.md) §5.3。
>
> 还有第三层：**读线程里只搬字节、解析放到窗口之后**。早先在读线程里直接解析每个包
> （建 dict + 496 B 的 `bytes`），GIL 上与主线程抢，Python 一停顿超过缓冲深度就真丢数据，
> 而且账会算到固件头上（记成 `s_usb_drop`）。改掉之后单变量 5 µs 档的丢包从 3.3% 降到 1.9%。
> 更根本的是：**`s_dropped`（探针跳拍）与 `s_usb_drop`（包缓冲耗尽）必须分开看** ——
> 前者是探针 CPU 的账，后者是主机排空的账，两者要的解法完全不同（脚本现在两个都报）。

4. **每次 SWD 传输的成本已经量到底：`36.2 × 指令/bit + 184 周期`**（实测拟合，见下表）。
   60 MHz 档是 401 周期/次：其中 `47 bit × 6 指令 = 282` 是位翻转，**剩下 184 周期
   是每次传输的固定开销（占 46%）—— 在 ACK/转向相位、blob 的进出场与 FGPIO 总线延迟上，
   每次传输都逃不掉**。一个样本 = 9 次传输（6 字的 span：TAR + prime + 5×DRW + RDBUFF）
   ≈ 9 × 401 + 组帧 ≈ 3990 周期 = **11.09 µs → 90.2 kHz**。

   **各档实测**（`script_test/scope_hss_test.py bench`，单变量 u32 = 3 次传输 /
   8 通道 6 字 span = 9 次传输；MCHTMR 24 MHz）：

   | SWD 档 | 指令/bit | 单变量（3 次传输） | `g_pack`（9 次传输） | 每次传输边际 |
   |---|---|---|---|---|
   | **60M** | 6 | **4.503 µs → 222 kHz** | **11.193 µs → 89.3 kHz** | 1.115 µs（401 周期） |
   | 45M | 8 | 5.244 µs → 191 kHz | 13.092 µs → 76.4 kHz | 1.308 µs（471） |
   | 30M | 12 | 6.849 µs → 146 kHz | 17.140 µs → 58.3 kHz | 1.715 µs（617） |
   | 20M | 18 | 9.298 µs → 108 kHz | 23.213 µs → 43.1 kHz | 2.319 µs（835） |

   线性拟合 `T = 36.2 × k + 184`（k = 指令/bit）在 6/8/12/18 四点上误差 <1%。

   **⇒ blob 这条路的账算清楚了：每减 1 条指令/bit 只省 36 周期/次传输。**
   - **6 → 5 需要 funnel shift（`fsri`）**：数据相位现在是 `bexti` + `or` + `rori`
     三条（掩码到 bit0、合并、循环右移），换成 `bexti` + `fsri` 正好两条。但
     `fsri` 要 Zbb 的广义移位，**本工程用的 `riscv32-unknown-elf-gcc 11.1.0` 直接
     报 `unrecognized opcode`**（GCC 11 的 binutils 还没收这条），而且 Andes 核是否
     实现它也无从验证 —— 只能靠 `.insn` 手搓编码去赌，赌输就是采样循环里一条
     非法指令把探针挂死。**收益 +7%（单变量）/ +9%（6 字 span），不值这个风险。**
   - **6 → 4 结构上不可能**：自然位序合并至少 3 条（掩码、移位、并入），加上 2 条
     CLK 边沿 + 1 条采样 = 6 条已经是这个结构的floor。要 4 条就必须让"掩码 + 并入"
     合成一条，而那要求 SWDIO 落在 DI 寄存器的 **bit 0 或 bit 31**（好接 `fsri`）——
     EVKLite 上 SWDIO = PA07（bit 7），`FGPIO` 的 `DI[p].VALUE` 就是一个引脚一位的
     32 位寄存器，没有"单引脚视图"可以借。**结论：现在的 6 指令/bit 就是最优解。**
   - 真正能动的是**184 周期那个固定开销**和**传输次数**：单变量 3 次传输里有 2 次是
     纯固定开销，9 次的 6 字 span 里固定开销占 46%。

   > 试过把 9 次传输批成一条 CMSIS-DAP `DAP_TransferBlock`（指望省掉 `swd_host` 的 C
   > 调用链）：**实测反而慢 6.5%**（12.31 vs 11.55 µs）—— 那 184 周期不在调用链里。
   > 代码留在 `scope_sampler.c` 作对照，**不要启用**。
   >
   > 顺带纠正先前的猜测：块读的每字成本**不随块长摊薄**（1024 B 与 2048 B 都是
   > 1.18 µs/字）⇒ 它是"每次传输"而不是"每块"的开销。

   **每次采样 ≈ `(字数 + 3) × 401 周期` + 框架开销**。那 3 次固定传输
   （TAR 写 + prime 读 + RDBUFF 读）对 6 字的 span 就是 **34% 的时间** —— 小 span 尤其贵。
   **单变量 u32 更极端：3 次传输里 2 次是"取结果"，真正的读只有 1 次。**

   > 🚨 **AP 写比 AP 读贵得多：656 周期 vs 401**。从"去掉一次 TAR 写省下 1.82 µs、
   > 而一次读传输只要 1.115 µs"反推出来的 —— 多出来的 ~255 周期是 posted write 的
   > 完成延迟（写完之后下一次 AP 访问要等它落地）。所以**采样循环里每一次"写"都
   > 值得盯**：省掉一次写等于省掉 1.6 次读。

   所以**天花板由 span 字数决定**，把纯传输量出来就一目了然
   （`rtt_read_bytes` 循环，无组帧）：

   | span | 纯传输 | 上限 |
   |---|---|---|
   | 2 字（8 B） | 5.82 µs | 172 kHz |
   | **4 字（16 B）** | **7.96 µs** | **126 kHz** |
   | 5 字（20 B） | ~9.2 µs | ~109 kHz |
   | **6 字（24 B，g_pack 那份布局）** | **10.30 µs** | **97 kHz** ← 墙 |
   | 8 字（32 B） | 12.65 µs | 79 kHz |

   **单变量 u32 的实测** —— 分水岭在"抱住 TAR"那条快路径（见下节）：

   | | 结果 |
   |---|---|
   | M0 探针侧能力 @60 MHz，**旧**（3 次传输） | 4.503 µs/样本 → 222 kHz |
   | M0 探针侧能力 @60 MHz，**新**（2 次传输） | **2.681 µs/样本 → 373 kHz** |
   | M0 @45 MHz（新） | 3.140 µs → 318.5 kHz（档位确实起作用） |
   | **实测零丢可持续（端到端）** | **251 kHz**（4 µs 周期 + 关掉 CDC 桥，丢 0.3%） |
   | 硬推 3 µs 周期 | 309 kHz，但丢 7.8%（其中探针侧约 3.7%）—— 拐点在 3 与 4 µs 之间 |
   | 同上（251 kHz 档）但 CDC 桥开着 | 242 kHz（丢 3.6%）—— **差距全在探针主循环**，见下节 |
   | 5 µs 周期（旧时代的"零丢"档） | 200.9 kHz，丢 0.3% |

   > ⚠️ **"单变量时档位不起作用"是个假象（已修）**：早先量到 1/20/45/60 MHz 读数
   > 一模一样，于是写成"时间全在固定开销上、位翻转不是瓶颈"。**真相是档位根本没换**：
   > `rtt_bridge_set_swd_clock()` 在桥那一侧链路没就绪时只把值**记下来**、不装载 blob，
   > 而采样器有自己一份 `s_swd_ready`；桥那份被 `rtt_bridge_stop()` / 主机碰 DAP
   > （`rtt_bridge_note_dap_activity()`）清掉后，采样器这份还是 1 ⇒ 既不重新初始化、
   > 也拿不到新档，**状态字里却报着新频率**。从慢档跳回快档时还会因为少了
   > `rtt_swd_init()` 里那段 20 MHz 斜坡而在第一次访问就 -4。
   > 现在采样器改用 `rtt_bridge_request_swd_clock()`（换挡一律走"下次重新初始化"），
   > 并且 `scope_link_ready()` 要同时看桥那份链路状态。修完 60 MHz 197.8 kHz、
   > 45 MHz 166.7 kHz —— 档位立刻能看出来了。
   >
   > **效率提示**：1 个 u32 = 3 次传输换 4 字节；**2 个相邻 u32 = 4 次传输换 8 字节**
   > （≈5.6 µs → 178 kHz 采两个通道）。多通道时"变量挨在一起"极其划算 ——
   > 每加一个相邻的 u32 只多 ~1.1 µs（22 个通道才到 24 字节）。
   **⇒ 6 字的 span 物理上到不了 100 kHz**（光传输就 9 × 401 = 3610 周期）。要 100 kHz+
   只能把被采样的变量排得更紧（少一个字就多 10%）。

   已经做掉的框架优化（86.6 → **90.2 kHz**）：
   - `swd_read_block4()`：span 天然 4 字节对齐、不跨页，跳过 `swd_read_memory()` 的头尾
     字节处理与分页循环，也跳过 `rtt_read_bytes()` 的分块循环；
   - 按变量宽度展开赋值代替 `memcpy()` —— 1/2/4 字节的 memcpy 会真的走一次函数调用，
     而一个样本有 8 个变量，实测这是框架开销的大头；
   - **零拷贝直读**：span 内变量首尾相接且 4 字节对齐时，直接把 SWD 读进包里该样本的槽位，
     省掉 `s_stage` 中转和一次 `memcpy` 调用（M0 4.546 → **4.478 µs**）；
   - 热路径上**去掉逐拍诊断计时**（`mchtmr_now()` 的 volatile 读在 20 万拍/秒这个量级上是实打实的开销）；
   - 周期换算成 MCHTMR tick **只在配置时做一次**，不再每拍做一次 64 位除法。

   > **试过但走不通的一条路（记下来免得再想）**：把 RDBUFF 读并进下一样本的流水
   > （指望 9 → 8 次传输）。**不成立** —— AP 的 DRW 读返回的是"上一次 **DRW 读**"的数据，
   > 而 RDBUFF 是 **DP** 读、不推进这条链。所以省掉 RDBUFF 读拿到的是**重复值**（上一次
   > DRW 的结果），不是缺的那个末字。**N+2 次传输是单次采样的下限。**
   >
   > 另外 `DAP_Data.clock_delay` 压到 0（`SCOPE_FLAG_DELAY0`）实测只有 0.2%、在噪声内
   > —— 高速档本来就设成 1，这条路也是死的。

### 单字快路径：**抱住 TAR，3 次传输压到 2 次（已做）**

上面把账算到"每次传输 401 周期、AP 写还要 656"这个粒度，于是**单变量 u32 的 3 次传输
里有 2 次纯属"取结果"**（TAR 写 + prime 读 + RDBUFF 读），而真正发起内存读的只有 1 次。
这三次的来历是 AHB-AP 的两条规矩：

1. **TAR 每次都要重写**，因为 CSW 里 `AddrInc=1`（自增），读完一个字之后 TAR 已经
   变成 `addr+4`。而 `swd_host.c` 原来只缓存了 CSW 与 DP_SELECT，**没有缓存 TAR**。
2. **AP 读是 posted 的**：DRW 读回来的是"上一次 DRW 读"的结果，所以每次采样都得
   配一次 RDBUFF 才能把当前值取出来。

**做法**（`swd_host.c` 的 `swd_read_word_held()`）：给单字 span 把 CSW 切成 `AddrInc=0`，
并给 `swd_write_ap(AP_TAR)` 加缓存 —— 地址没变就整趟跳过。稳态每拍只剩
`DRW + RDBUFF` = **2 次传输**。

| | 每次采样 | 探针侧上限 |
|---|---|---|
| 旧（3 次传输，含一次 AP 写） | 4.503 µs | 222 kHz |
| **新（2 次传输）** | **2.681 µs** | **373 kHz（+68%）** |

实测端到端（关掉 CDC 桥）：**4 µs 周期 → 251 kHz，丢 0.3%**；3 µs 周期 → 309 kHz 但丢 7.8%。
**167 kHz（本次会话起点）→ 251 kHz，累计 +50%。**

> ⚠️ **必须整段会话都走这条路**：CSW 切成不自增之后，只要夹进一次多字块读
> （`swd_read_block`）就会把它切回自增，来回切一次是 2 次传输（而且 AP 写贵，
> 实际是 ~1.3 µs），省下的那点立刻赔光。所以采样器只在**所有 span 都是 4 字节直读**
> 时才启用（`s_all_word`）。混合配置（一个 3 字 span + 一个单字 span）实测 M0
> 11.53 µs，跟"全程不自增"的 ~15.8 µs 判然有别 —— `--set mixed` 就是守这条守卫的用例。
>
> ⚠️ **TAR/CSW 缓存的失效点**：AP 的 `AddrInc=1` 会把 TAR 带跑，所以每一处 DRW 访问
> （读或写、块读还是单字、成功还是失败）**之前**都要把 `tarp_ok` 置 0；
> 主机自己碰过 DAP 之后（`rtt_bridge_note_dap_activity()`）还要把 select/csw/tar
> 三个影子寄存器一起作废 —— 主机那条路走 `SWD_Read/SWD_Write`，完全绕过 swd_host。
> 方向要保守：多写一次 TAR 只是慢，判反了就是读到**别的地址**。

### 下一个杠杆：**再流水一拍（1 次传输）+ 打破 1 µs 的周期粒度**

- **1 次传输**：只发一次 DRW 读、把结果留到下一次采样取（时间戳整体后移一个周期，
  是常数偏移不是误差），单变量就只剩 1 次传输。按 `401 + 163` 估约 1.57 µs → 637 kHz。
  当前 3 µs 周期丢 7.8% 正是因为采样要 2.681 µs、余量只剩 ~0.3 µs；降到 1.57 µs 之后
  3 µs 就有 1.4 µs 余量，**333 kHz 可以零丢**。代价是推包逻辑要能"回填上一个样本的槽位"，
  且包填满时得先补一次读。
- **周期粒度**：`period_us` 是整数微秒，而采样只要 2.681 µs —— 拐点在 3 与 4 µs 之间，
  再往上没有档位可用。要吃到 3.5 µs 这种中间值，得让协议支持亚微秒周期
  （加一个 `period_ns` 字段或"周期以 1/4 µs 为单位"的 flag），并把时间轴一起改。
  这需要网页侧配合，所以先不动。
- **USB 侧**：251 kHz × 4 B ≈ 1.03 MB/s 还算安全；333 kHz 就是 1.37 MB/s，
  接近命令行 pyusb 读法的天花板（~1.7 MB/s），网页侧多条 transferIn 在飞才稳。

### 端到端 vs 探针能力：**差在主循环，不在 SWD**

M0 标定量的是**纯采样循环空转**，不含主循环其余部分、不含推包、不含 USB。

```
5 µs 周期 = 1800 周期预算
一次采样  = 1612 周期（4.478 µs）   ← 与 SWD 时钟档位几乎无关
余量      =  188 周期               ← 主循环其余部分必须挤进这 188 周期
```

于是逐项砍主循环的非采样开销：

| 改动 | 5 µs 周期端到端 | 丢包 |
| --- | --- | --- |
| 起点（6 µs 周期，零丢） | 167 kHz | 0.3% |
| 5 µs 周期，什么都不改 | 170 kHz | 15.3% |
| **关掉 CDC/串口桥**（HID 0x34，见下） | 183.9 kHz | 8.4% |
| + 按键轮询分频（1 秒的按住时长不需要每轮读 GPIO+MCHTMR） | 186.5 kHz | 7.1% |
| + 周期 tick 预换算（省掉每拍一次 64 位除法） | **194.5 kHz** | 3.1% |
| + 读数脚本改成窗口后解析（见下"主机侧读法"） | **197.4 kHz** | **1.9%** |

剩下那 1.9% 里只有一小部分是探针跳拍：**`SCOPE_FLAG_DISCARD`（只采样不推 USB）
下 5 µs 周期是 1.0% 丢拍、6 µs 周期是 0.0%**。

> 这一段到此为止（5 µs 周期 197 kHz）。**后面单字快路径把一次采样从 4.478 µs 压到
> 2.681 µs，于是 4 µs 周期成了新的零丢档 → 251 kHz**；主循环那几百周期不再是瓶颈，
> 真正的限制变成了"周期只能是整数微秒"。见前面的「单字快路径」一节。

**CDC/串口桥开关（HID `0x34`，协议见 `Custom HID Protocol.md` 第 18 条）**
主循环每轮都要服务 CDC 桥：读一次 DMA 的 `DSTADDR`、两次 `disable_global_irq`、
三次环形缓冲查询 —— 正是上面说的那几百周期。现在有两种关法：

- **`SCOPE` flags bit5（`SCOPE_FLAG_CDC_OFF`，推荐）**：采样期间自动关、停采样自动恢复。
  网页只要在已有的 CONFIG 报文 flags 里加一位，不用自己管状态。只恢复"自己关过的那一次"，
  不会覆盖手动关掉的状态。
- **HID `0x34` 手动总开关**：`action=1` 带 0/1 设置，`action=0` 查状态（bit0 = 桥是开的）。
  给面板做显式勾选框用。状态不持久化，探针复位即恢复为开。

代价：暂停期间 **COM 口不通**（CDC 的 bulk OUT 被 NAK，主机自行重试），RTT-over-USB 也停。
采样数据走的是另一条 bulk IN `0x83`，与本开关无关；而且采样器与 RTT 桥本来就互斥
（`SCOPE` 的启动分支会先 `rtt_bridge_stop()`）。恢复时会丢掉暂停期间积压的串口数据并把
DMA 定位追平，不会灌一整圈陈旧字节给主机。

### 复测记录：F103ZE（512K flash / 64K RAM）+ 好线材

换到 ZE 之后**探针侧的数字和 C8 上完全一样** —— 说明瓶颈在探针 CPU，跟目标板/线材无关：

| 变量组 | span | M0 @60 MHz（2026-09-28 定稿） | 早期的 C8 数据 |
|---|---|---|---|
| 单个 u32 | 1 | **2.681 µs → 373.1 kHz**（单字快路径） | 4.536 µs → 220.5 kHz |
| `g_pack` 8 通道 | 1 | **11.193 µs → 89.3 kHz** | 11.092 µs → 90.2 kHz |
| `cross` 8 通道（跨 3 span） | 3 | 25.6 µs → **39.1 kHz** | 预测 25.7 µs ✓ |
| `mixed`（3 字 span + 单字 span） | 2 | 11.527 µs → 86.8 kHz（**不走**快路径） | — |

**好线材的收益在"长跑稳定"上**：60 MHz 档连续跑 20 s，`swdErr = 0`（一次 SWD 读错都没有）。

> ⚠️ 早期那条"60 MHz 与 45 MHz 都是 1083 KB/s、都丢 4.7% ⇒ 跟 SWD 无关"的结论**别再用**：
> 那次两档读数一样是因为**档位压根没换**（见上一节的 bug），而不是"USB 背压盖过了 SWD"。
> 修完之后同样的对比是 60 MHz 197.8 kHz / 45 MHz 166.7 kHz，档位差 19%。

**探针与 USB 两段要分开看**（用 `SCOPE_FLAG_DISCARD` 把 USB 那段摘掉即可量出探针本体）：

| 变量组 | 周期 | 端到端（带 USB） | 丢包 | 其中 USB 缓冲耗尽 | **纯采样（DISCARD）** |
|---|---|---|---|---|---|
| 单个 u32 | **4 µs** | **251.0 kHz** | **0.3%** | 100% | — |
| 单个 u32 | 3 µs | 309.0 kHz | 7.8% | 52% | — |
| 单个 u32 | 5 µs | 200.9 kHz | 0.3% | 100% | ~198 kHz，丢 **1.0%** |
| `g_pack` 8 通道 | 12 µs | 76.4 kHz | 8.8% | **100%** | 86.6 kHz，丢 **0.0%** |
| `cross` 3 span | 30 µs | 33.4 kHz | 0.2% | 100% | — |

即：**单变量那条路探针本体已经贴着 373 kHz**（余下的账在中断抖动 + 每 124 个样本
一次的推包尖峰 + 主机排空）；而 `pack`（每包只装 20 个样本、要 ~1.7 MB/s）**卡在 USB
包率上**（~3500 包/s ≈ 1.75 MB/s 天花板 —— 主机读缓冲从 512 B 试到 128 KB 都一样，
设备速度确认是 High Speed、`0x83` 确认是 bulk/512 B/mps 512）—— 这是**既有问题、
与 CDC 开关和单字快路径都无关**，下一条要啃的是它（把多个 512 B 包并成一次
`usbd_ep_start_write`，或查 DWC2 那条 IN 的在飞深度 / TxFIFO）。

> 🚨 **换板子要改链接脚本**：scope 例程原本只有 C8（64K/20K）的 ld。现在 `build.ps1` /
> `flash.ps1` 都带 `-Board ze|c8`，**默认 ze**、产物固定落在 `build\`（check.py 认这个路径）。
> ZE 与 C8 的变量地址**实测完全一致**（同一份链接布局），所以测试脚本里的地址不用改。
### 复现

```bat
:: 靶子（STM32F103C8，96 MHz 超频，10 kHz 契约波形：正弦/三角/方波/锯齿/撕裂自检/50 kHz 混叠源）
cd script_test\stm32f103_scope
pwsh -File build.ps1
pwsh -File flash.ps1
python check.py                      :: 客观验收：halt → dump RAM → 逐项核对契约 + 反测时基

:: 探针侧（命令行验收，不等网页）
python script_test\scope_hss_test.py bench --clock 60000000     :: M0 标定（µs/样本 → 上限 kHz）
python script_test\scope_hss_test.py run --clock 60000000 --period 40 --secs 3

:: 上限在哪：单变量 u32、4 µs 周期、关掉 CDC 桥（flags bit5 = 0x20）⇒ 251 kHz
python script_test\scope_hss_test.py run --set one   --clock 60000000 --period 4  --secs 4 --flags 0x20
:: 再快就只有 3 µs 这一档（会丢 ~8%）
python script_test\scope_hss_test.py run --set one   --clock 60000000 --period 3  --secs 4 --flags 0x20
:: 只量探针本体（不推 USB —— 把 USB 那段摘出去，丢包就全是探针 CPU 的账）
python script_test\scope_hss_test.py run --set one   --clock 60000000 --period 4  --secs 4 --flags 0x22
:: 混合 span（守"所有 span 都是单字才走快路径"那条守卫；走错了会明显更慢）
python script_test\scope_hss_test.py bench --set mixed --clock 60000000 --iters 300
:: 多通道（每包 20 个样本，会被 USB 包率卡住）
python script_test\scope_hss_test.py run --set pack  --clock 60000000 --period 12 --secs 4 --flags 0x20
:: 手动总开关（网页面板那条命令）
python script_test\scope_hss_test.py status --bridge off     :: 之后记得 --bridge on 还回去
```

> ⚠️ **别拿 `rtt_bridge_sweep.py` 对着没有 RTT 控制块的目标跑**（比如现在这块跑 scope
> 例程的 F103ZE）：它的第 3 阶段会启动 RTT 桥并反复重扫，实测把探针主循环拖到不再应答
> HID，只能**拔插一次**才能恢复。纯 SWD 块读那两阶段（3301 KB/s）是可以跑的。

## 文档

| 文档 | 内容 |
| --- | --- |
| [`docs/hpm6800evk-jtag.md`](docs/hpm6800evk-jtag.md) | **HPM6800EVK（HPM6880，RISC-V）用本探针调 JTAG 的完整记录**：接线坑、启动头真相、DMI/SBA 引擎与专用汇编、三个 DTM 时序坑、RTT 交付率与跨后端极性 bug、TCK 频率上限 |
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
