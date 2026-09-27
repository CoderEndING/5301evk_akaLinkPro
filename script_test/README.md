# script_test

akaLinkPro (HPM5301) 固件测试脚本集合。这些脚本用于验证 **UART ↔ CDC 串口桥**、
**DAP 模式切换**（SWD/空闲 = UART，JTAG = FGPIO）以及设备枚举状态。

## 回环接线（两台板子不同！）

| 板子 | CDC VCOM 引脚 | 回环短接位置 |
| --- | --- | --- |
| akaLinkPro | UART2 PA08(TXD)/PA09(RXD) | PA08 ↔ PA09 |
| HPM5301EVKLite | UART3 PB15(TXD)/PB14(RXD) | **J3.8 ↔ J3.10**（丝印 UART_TXD/UART_RXD） |

> 接逻辑分析仪**不算**回环，必须把 TX 和 RX 两个脚真正短接。

## 依赖

- Python 3.11 + `pyserial`、`pyusb`（`pip install pyserial pyusb`）
- Windows 上需已安装 libusb/WinUSB 驱动（pyusb 后端）
- 设备运行 `application_5301` 固件，并在系统中枚举出 CDC 串口（COMx）

COM 端口号在不同机器上会变化，脚本支持通过参数传入，默认 `COM75`。
EVKLite 上 CDC 是复合设备的 `MI_01`，可用
`Get-PnpDevice -PresentOnly | Where-Object InstanceId -match 'VID_0D28&PID_0204&MI_01'` 确认。

## 脚本说明

| 文件 | 用途 |
| --- | --- |
| `uart_loopback.py` | 串口回环压力测试：逐字节回环 + 大块随机数据流，覆盖 115200~2Mbps |
| `uart_loopback_hs.py` | 高速回环测试：2Mbps~11.25Mbps，含吞吐量与线速效率统计 |
| `uart_loopback_common.py` | 常见速率回环详表：请求/实际波特率、误差、耗时、吞吐、线速效率 |
| `diag_loopback.py` | 单次大流量回环并定位首个错误位置/零区块，用于诊断 DMA 边界问题 |
| `test_modeswitch.py` | 通过 CMSIS-DAP 发 `DAP_Connect`(JTAG/SWD)/`DAP_Disconnect`，验证引脚复用切换 |
| `uart_stress_loop.py` | 定时长回环压测（发一块读一块），统计 errors/late/最大延迟，可区分丢失与延迟 |
| `uart_stress_stream.py` | 连续流回环压测（读线程 + 不丢包比对），用于与 SWD 并发时做满负荷验证 |
| `list_usb.ps1` | 列出 `VID_0D28` 相关 USB 设备与 CDC 端口（状态检查） |
| `hold_port.py` | 打开串口并保持若干秒，便于用 J-Link/GDB 在线检查运行状态 |
| `swd/run_benchmark.py` | 生成指定 `adapter speed`/`iterations` 的 OpenOCD SWD 读写校验并运行 |

> `stm32f103_rtt_speed/` 是 RTT 测速用的 STM32F103 测试固件（SEGGER RTT + 计数
> 全局变量），`rtt_*.py` 需要它编译出的 `build/fw.bin`：先跑
> `powershell -File stm32f103_rtt_speed\build.ps1`（需要 arm-none-eabi 工具链），
> 或自带一份已编译的 `fw.bin`（`build/` 不入库）。
> `sram_speed_test.py` / `rtt_*.py` 走 OpenOCD，路径由环境变量
> `OPENOCD_EXE` / `OPENOCD_SCRIPTS` / `HPM_SDK_ENV_DIR` 覆盖。

### EVKLite（HPM5301EVKLite）专用

| 文件 | 用途 |
| --- | --- |
| `evk_echo.py` | **最常用**：打开 CDC 写 27 字节并比对回环，退出码 0=PASS |
| `evk_flash.py` | HID `CMD_ENTER_DFU` → DFU 虚拟盘拖入 `_pack.bin` → 等 APP 回来 |
| `evk_dfu_enter.py` | 只发 HID 进 DFU 命令（不烧录） |
| `evk_jtag_mode_test.py` | 验证切到 SWD+JTAG 模式后 COM 口仍可用（引脚复用回归） |
| `evk_probe.py` | 快速探测：CDC 有无自发数据 + HID 应答格式 |
| `evk_ident.py` | 读运行中固件的版本/编译时间/参数（确认板上到底是哪一版） |
| `evk_pyocd_sram.py` | 同一 SRAM 测速改用 pyOCD（换一套主机栈交叉验证瓶颈在哪一侧） |
| `evk_swd_probe.py` | 固定 SWD 时钟做传输并打印 OpenOCD 的完整日志（找重试/ACK 异常） |
| `evk_target_clock_test.py` | 读 STM32F103 的 RCC，对比「复位默认 8MHz」与「提到 64MHz」下的吞吐 |
| `evk_verify.py` | 带 HID 状态读数的回环验证（需 TEMP-DIAG 诊断固件） |
| `evk_diag.py` | 全量诊断（需诊断固件）：引脚直连自测 / UART 内部回环 / 计数器 / force-start |
| `evk_watch_jumper.py` | 打开 TX 心跳并实时轮询 RX 计数，用于边插跳线边观察（需诊断固件） |
| `rtt_probe_bridge.py` | **探针侧 RTT 桥测速**：boost 目标 → HID `CMD_RTT` 启动桥 → 只读串口测吞吐，并做全流零丢包校验（分支 `feat/probe-rtt-bridge`） |
| `rtt_bridge_sweep.py` | **调优扫描**：时钟 × clock_delay × 块大小，并排给出「纯 SWD 基准 / 丢弃模式搬运 / 端到端」三个数，直接指出天花板在哪一侧（`--clk=` 指定桥的请求档位） |
| `rtt_peek.py` | 读探针自身内存（HID `CMD_RTT` action 5），bring-up 期查 `DAP_Data`/trace 用 |
| `rtt_rawdap.py` | 经探针主循环透传原始 CMSIS-DAP 请求（action 4/6），bring-up 期用 |

### RTT 测速为什么慢（实测结论，2026-09-27）

`make rtt-test` 早期只有 ~0.4 MB/s，比 SRAM 读写的 2.5 MB/s 差 6 倍。逐项量下来是
**三个原因叠加**，与探针固件无关：

**① RTT 的「生产者」跑在目标机上（主因）**：STM32F103 上电默认 HSI 8 MHz，此时
固件的 `SEGGER_RTT_Write` 死循环只能产出 **277 KB/s**，把 SWD 从 10 MHz 提到
20 MHz **一点不变**（276.8 → 277.1 KB/s）——瓶颈在目标，不在链路。

**② RTT 是「轮询」模型，不是流式**：每次取数要 3 个 host↔探针来回（读
WrOff/RdOff → 读环形缓冲 → 写回 RdOff），每次轮询约 3 ms 固定开销。SRAM 的
`load_image/dump_image` 是**一次大块流水传输**，所以能跑满 2.5 MB/s；轮询做不到。

**③ 目标运行中，长块读会失败**：运行的内核会与 AHB-AP 抢总线，一次 12 KB 的
块读会成百次失败（吞吐掉到 76 KB/s），拆成 1 KB 分块后 **0 失败**。所以轮询读
必须限长 + 重试。

实测阶梯（目标均已归一化到 64 MHz）：

| 配置 | 吞吐 |
| --- | --- |
| 目标停在复位默认 8 MHz（任何 SWD 时钟） | 277 KB/s ← 原来的 0.4 MB/s 就是它 |
| `make rtt-test`（OpenOCD rtt server），目标 8 MHz | 403 KB/s |
| `make rtt-test`，目标 64 MHz | **919 KB/s**（20 MHz 档） |
| `make rtt-max`（轮询跑在 OpenOCD 内、32 位分块读、12 KB 环） | **1140 KB/s**（60 MHz 档，0 失败） |

配套改动：RTT 环形缓冲 4 KB → **12 KB**（4 KB 时每次取数都撞满 `maxdrain=4095`，
轮询次数被白白吃掉）；`rtt-test` 会先 halt → 提频 → resume 并打印实测主频。

> 想再往上只有一条路：**把轮询下沉到探针固件里**（探针自己用 SWD 轮询目标的 RTT
> 控制块，再经 USB 推给主机），这样就没有主机往返开销，理论上限回到 SWD 读带宽
> （~2 MB/s）。J-Link 就是这么做的，属于固件新功能。

**这条路已经在分支 `feat/probe-rtt-bridge` 上做完了**，实测：

| 配置 | 吞吐 |
| --- | --- |
| 探针侧桥，20 MHz SWD | 1420 KB/s |
| 探针侧桥，36 MHz SWD | **2190 KB/s（2.14 MB/s）**，全流 13.5 MB 零丢包零重包 |

`python rtt_probe_bridge.py COM52 6 36000` 一把跑完（自动 boost 目标 → 启动桥 →
只读串口测速 → 校验流完整性 → 读回状态字）。实现要点、三个坑（握手/提速顺序、
RdOff 幂等重试、512 B 分块）见
[`docs/HPM5301EVKLite_port.md` §5](../docs/HPM5301EVKLite_port.md#5-探针侧-rttcdc-桥分支-featprobe-rtt-bridge)。

`rtt_speed_test.py` 走的是 OpenOCD 自带的 rtt server，它的 `rtt polling_interval`
**默认 100 ms**，实测（20 MHz SWD、目标 64 MHz）：

| polling_interval | 吞吐 | 轮询/秒 |
| --- | --- | --- |
| 不设置（默认 100 ms） | 9.2 KB/s | 9 |
| 10 ms | 64.1 KB/s | 64 |
| 5 ms | 64.3 KB/s | 64 |
| **1 ms**（脚本采用） | **903.9 KB/s** | 904 |
| 0 ms（不限速） | 906.1 KB/s | 906 |

- **必须设成 1 ms**：默认值只有 9 KB/s（每次轮询只搬约 1 KB，100 ms 一次 = 9 KB/s）。
- 1 ms 再往下没有收益（0 ms 与 1 ms 等价），所以脚本保持 1 ms。
- 5~10 ms 反而异常差（64 KB/s）——OpenOCD 的定时器在这个区间量化得很糟，别在这个
  范围里"调优"。
- 注意这个参数**只影响 `make rtt-test`**；`make rtt-max` 的轮询跑在 OpenOCD 内部，
  与它无关。
- 低 SWD 时钟下这个参数也救不了：1 MHz 时 1 ms 间隔也只有 ~79 KB/s（受 SWD 读出
  速度限制）。

### SRAM 测速为什么必须固定目标主频 ⚠️

SWD 的每次 AHB-AP 事务要花掉目标机好几个 HCLK，所以吞吐上限是
`min(SWD 时钟, 目标 HCLK)`。STM32F103 **上电后 RCC 默认是 HSI 8MHz**，此时无论把
SWD 时钟拉到 45/60MHz，吞吐都会卡在 ~1.4MB/s；而 STM32F1 的 `reset halt` 是
**核心级复位、不会清 RCC**，所以只要之前有固件把 PLL 配起来过，测速又会变成
~2.8MB/s —— 同一个探针、同一份固件，数字却能差一倍。

`sram_speed_test.py` 因此默认在每轮 `reset halt` 之后把目标提到
**64MHz（HSI/2 ×16，无需外部晶振）**，并打印实测的 SYSCLK/HCLK：

```bat
python script_test\sram_speed_test.py              :: 归一化目标主频（推荐，数字可复现）
python script_test\sram_speed_test.py --no-boost   :: 保留目标复位后的原始状态
```

### 运行示例

```bat
python script_test\uart_loopback.py COM75
python script_test\uart_loopback_hs.py COM75 262144
python script_test\uart_loopback_common.py COM75
python script_test\diag_loopback.py COM75 115200 32768
python script_test\test_modeswitch.py COM75
powershell -ExecutionPolicy Bypass -File script_test\list_usb.ps1
python script_test\hold_port.py COM75 8
```

### 串口速率上限与就近取整（HPM5301）

- 驱动器公式：`baud = uart_clk / (div * osc)`，`osc` 为 8~30 的偶数。
- **默认（符合手册）**：UART2 时钟 = `PLL0CLK0(720MHz)/9 = 80MHz`（UART 输入时钟手册上限），
  硬件/软件上限 = `uart_clk/8 = 10 Mbps`（`UART2_CLK_DIV=9`、`UART2_MAX_BAUDRATE=10000000`）。
- **超频选项**：`CMakeLists.txt` 里取消注释 `sdk_compile_definitions(-DUART2_OVERCLOCK=1)`，
  则 `720/4 = 180MHz`、上限 22.5 Mbps，且 11.25/15/18 Mbps 可精确生成。
  **注意：180MHz 超出手册限制，不保证所有芯片稳定，仅供测试。**
- 无法精确生成的按**就近取整**（`uart2_round_baudrate()`）。
- 实际写入的波特率存于 `g_uart2_applied_baud`（可用 J-Link/GDB 读取核对）。
- 实测吞吐（默认 80MHz）：10M→~972KB/s（线速效率 99.6%）；>10M 请求被钳到 10M。
  超频 180MHz：22.5M→~2108KB/s。

## gdb/ — 在线调试检查脚本（配合 JLink GDB Server）

先启动 GDB Server（或用 `firmware/application_5301/gdb_server.bat`）：

```
JLinkGDBServerCL.exe -device HPM5301xEGx -if JTAG -speed 4000 -port 2331 -nogui -singlerun
```

然后：

```bat
riscv32-unknown-elf-gdb -batch -x script_test\gdb\<script>.gdb ^
  firmware\application_5301\build_dfu\output\akaLinkPro_App.elf
```

| 文件 | 用途 |
| --- | --- |
| `dbg_app.gdb` | 连接/复位/load/命中断点 main（与 VSCode launch 流程等价） |
| `dbg_test.gdb` | 连接后 reset/load/break main/查看寄存器 |
| `inspect*.gdb` | 读取 UART2 寄存器、DMA 通道、`dma_resource_pools`、引脚 FUNC_CTL 等 |

> 注意：`inspect*.gdb` 中的寄存器地址/符号对应 HPM5301 + 本工程，改动后需同步。

## jlink/ — J-Link 命令行测试片段

用法：`JLink.exe -NoGui 1 -ExitOnError 1 -CommanderScript script_test\jlink\<script>.jlink`

| 文件 | 用途 |
| --- | --- |
| `jl_probe.jlink` | `ShowEmuList`，列出已连接的 J-Link |
| `jl_min.jlink` | 最小 JTAG 连接测试 |
| `jl_test3.jlink` | JTAG 自动探测 + 连接 + 读内存 |
| `jl_read.jlink` | 读 flash（APP 签名 `HPM!`） |
| `jl_pins.jlink` | 读 PA08/PA09/PB13 的 IOC FUNC_CTL 与 GPIO 输出状态 |
| `jl_load.jlink` | `erase` + `loadfile` 示例（路径需按需修改） |
| `jl_flashinfo.jlink` / `jl_help.jlink` / `jl_test.jlink` / `jl_test2.jlink` | 早期调试片段 |

## CDC + SWD 同时满载

SWD 压测（`swd/run_benchmark.py`，目标可为 STM32F1，`adapter speed` 20/36/45/60MHz）
与 CDC 连续流回环（`uart_stress_stream.py COM75 9000000 <秒>`）同时运行。

实测（SWD 1000 轮 + CDC 9Mbps 连续流）：

| SWD 速度 | CDC 速率 | SWD 校验 | CDC 连续流 |
| --- | --- | --- | --- |
| 20 MHz | 9 Mbps | 1000/1000 PASS | 8/8 OK（每轮 ~7MB） |
| 20 MHz | 10 Mbps | 1000/1000 PASS | 8/8 OK |
| 20 MHz | 22.5 Mbps | 1000/1000 PASS | 16/16 OK（2 轮，~230MB） |
| 36 MHz | 9 Mbps | 1000/1000 PASS | 全 OK |
| 45 MHz | 9 Mbps | 1000/1000 PASS | 全 OK |
| 60 MHz | 9 Mbps | 1000/1000 PASS | 全 OK |
| 60 MHz | 22.5 Mbps | 1000/1000 PASS | 4/4 OK |

说明：SWD 20MHz 的临界区抖动最大；即使 `20MHz SWD + 22.5Mbps CDC`（双向极限）
两轮 16/16 也全部干净，无掉/重数据。偶发失败需先排查硬件接触。

结论：SWD 20~60MHz 与 CDC 9Mbps **同时满载无掉数据/无重复**。
（关键修复：给 `g_uartrx` 的 DMA-TC 生产者补齐临界区；并用 GPTMR 定时器驱动 RX flush，
不依赖 IDLE/满缓冲中断。）

RX flush 定时器周期**按波特率动态调整**（目标每次约 512 字节，clamp 到 200us~10ms）：
低波特率时降低中断频率。实测 GPTMR RLD：`9600 → 10ms`、`9M → 568us`。

## 关键结论（回归基线）

- 回环压力测试：115200 / 460800 / 921600 / 1M / 2Mbps **全部通过**。
- 常见速率（`uart_loopback_common.py`）：9600~9M **全部通过**；
  6M→5.625M、8M→7.5M、10M→9M、11.25M→9M（9M 软件上限 + 就近取整）；高速线速效率 ~99.7%。
- 高速回环（1MB×多次，稳定）：2M~9M **全部通过**，线速效率 ~100%。
- 模式切换：空闲回环 OK → JTAG 下 COM 无回显（正常）→ SWD 恢复 OK → Disconnect 恢复 OK。
- 引脚状态：PB13 `FUNC_CTL=0` 且输出高（5V_EN 开）；空闲/SWD 下 PA08/PA09 `FUNC_CTL=2`(UART2)。
- RX 采用 **DMAV2 infinite-loop 圆形缓冲**（无 disable/restart），消除了重启边界上的重复字节。
