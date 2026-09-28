# HPM6800EVK（HPM6880, RISC-V）用 akaLinkPro 探针调 JTAG —— 打通与提速

> 结论一句话：**RISC-V 走 JTAG 时，主机侧驱动（OpenOCD）是往返延迟受限的，
> 96~154 KB/s 就是天花板；把搬运搬进探针固件后到 1.19 MB/s（约 9 倍）**，
> 而这个提升的关键不是"JTAG 汇编优化"本身（那部分只占 +24%），
> 而是**让探针自己发 DMI 扫描**、彻底干掉每次访问一个 USB 往返。

---

## 1. 硬件与接线（先说两个坑）

| 项 | 值 |
|---|---|
| 目标 | HPM6800EVK（板上 SoC = **HPM6880**，双核 RISC-V，TAP IDCODE `0x1000563D`） |
| 探针 | **HPM5301EVKLite 移植板**（固件 `build_dfu_evklite`，CDC=COM5，output_mode=1 = SWD+JTAG） |
| 链路 | 探针 J5 20 针 ⟷ EVK **CN1** 20 针牛角座，EVK 上 TRST/TDI/TMS/TDO/TCK 五个跳线帽必须拔掉 |
| RISC-V | **只有 JTAG，没有 SWD**；`transport select jtag` |
| 探针 JTAG 引脚 | `TCK=PA06 TMS=PA07 TDI=PA05 TDO=PA04`（`-DJTAG_PIN_*_SHIFT` 按板子传） |

### 坑 ①：20 针排线的第 15 脚会打到自己

HPM5301EVKLite 的 **J5.15 就是它自己的 `RESET_N`**（README 早有警告），而 CN1.15
是目标 nSRST。于是 **OpenOCD 一 assert srst，探针自己先被复位掉** —— 现象是
OpenOCD 卡死、探针从 USB 上消失（本次复现过两次）。所以本仓库的 cfg 用：

```tcl
reset_config none      # 走 Debug Module 的 ndmreset，不碰 SRST 引脚
```

`reset_config srst_only`（SDK 自带 `probes/cmsis_dap.cfg` 的默认）在这套接线下
**不能用**。探针掉线后的恢复脚本：`script_test/probe_usb_recover.ps1`（给它所在
的那个 USB hub 断电重上，因为板子是 USB 供电的，顺带复位 MCU）。

### 坑 ②：`adapter speed` 对 JTAG 扫描无效

`JTAG_Sequence()` → `JTAG_Sequence_GPIO_ASM_45M(info, tdi, tdo, 0)` **把 delay 写死
传 0**，汇编里也根本没读这个参数，而时序是 `.equ` 常量。所以 JTAG 侧
`adapter speed` 是个摆设（SWD 侧有 6 档运行时加载的 blob，JTAG 没有）。

---

## 2. 阶段一：调试跑通

```bat
:: 探针切 JTAG（TDI/TDO 与 VCOM 共用引脚，必须切模式）
python script_test\hpm6800_probe.py set-mode 1
:: OpenOCD 接目标
openocd -s <sdk>\tools\openocd\tcl -s <sdk>\hpm_sdk\boards\openocd ^
        -f script_test\openocd_hpm6800evk_dap.cfg -c "init" -c "halt" -c "reg" -c "shutdown"
```

实测全部通过：TAP 识别 → `datacount=4 progbufsize=8` → `XLEN=32 misa=0x4094112d`
→ halt / 寄存器 / 内存读写（写 `0xDEADBEEF` 读回一致）/ 擦写校验 NOR flash
（`flash write_image erase` 40 KiB/s，写完后 `mdw` 读回的字节与 `.bin` 逐字节一致）。

**烧录 HPM6800EVK 不需要 J-Link**：SDK 的 `boards/openocd/boards/hpm6800evk.cfg`
带 `hpm_xpi` flash driver，本探针在 JTAG 下直接就能烧：

```bat
python script_test\hpm6800_flash_target.py <image.bin>
```

---

## 3. 阶段二：SRAM 读写速度（两种口径）

`script_test/sram_speed_hpm6800.py`（主机侧、OpenOCD `load_image`/`dump_image`，
AXI_SRAM `0x01200000`）：

| OpenOCD 后端 | 写 | 读 |
|---|---|---|
| `progbuf`（默认） | 154.0 KB/s | **95.9 KB/s** |
| `sba` | 87.0 | 85.3 |
| `abstract` | 14.5 | 14.4 |

换后端救不了。原因是**往返**：在 OpenOCD 内部用 Tcl 循环量到单次 DAP 命令
往返 ≈ **97 µs**，而读 40 µs/字、写 26 µs/字，正好等于"**每个 abstract command
（最多 4 字）一个 USB 往返**"。所以主机侧驱动 RISC-V 天生就是延迟受限的，
探针侧再快也没用。

---

## 4. 阶段三：探针侧 RISC-V 引擎（核心工作）

新增 `firmware/application_5301/src/riscv/`：

- `riscv_jtag.c` —— JTAG TAP 导航 + DMI 41 位请求/响应流水线 + **SBA
  （System Bus Access）块搬运**（`sbautoincrement` + `sbreadondata`，
  稳态 **一次 DMI 读 = 一个 32 位字**）；
- `riscv_svc.c` —— 延迟执行层（JTAG 位操作**不能**在 USB 中断里跑，
  与 RTT 桥同一规矩），HID `CMD_RISCV (0x32)`。

数据通路：`sba_read/sba_write` 用的是**系统总线地址**，所以 RTT 控制块要放在
地址两侧一致的 RAM（本芯片 `flash_xip` 的 `.bss`/noncacheable 都落在
**AXI SRAM 0x01200000 起**，正合适；ILM/DLM 是核内地址 0x0/0x80000，
系统总线要走 0x01040000/0x01060000 别名，控制块里存的缓冲指针会对不上）。

### 踩过的三个 DTM 坑（都是"看起来像协议问题"的时序/编码问题）

1. **DTM 要求每次 DR 扫描之间插 Run-Test/Idle 周期**：`dtmcs.idle` 读出就是
   **7**。不插 idle 时 DTM 会**静默停摆**——响应冻结在 `op=3`，看起来跟
   "dmcontrol 写入被拒绝" 一模一样。实测 ≥4 拍即可，这里取 8。
2. **41 位 DR 里 `op` 在 bit[1:0]、data 在 bit[33:2]、addr 在 bit[40:34]**，
   响应里 addr 回显的是**上一个操作**的地址（流水线一拍深）。
3. `dmstatus` 在 `dmactive=0` 时读出来是 0；`dmcontrol.dmactive` 必须先写 1。

### 专用汇编：`JTAG_DP_GPIO_ASM_DMI.S`

把**一次完整 DMI 访问**（idle → Select-DR → Capture-DR → Shift-DR(41) →
Exit1-DR → Update-DR → RTI）收进一个函数，41 位请求直接在寄存器里逐位右移，
TDO 一拍流水采样。替代原来"6 次 `JTAG_Sequence` 调用 + C 侧打包 TDI 字节"
（那部分 C 开销约 1.25 µs，占一次访问的 30%）。

三个踩过的坑（都修了，都在注释里）：

| 现象 | 根因 |
|---|---|
| 响应 `op/addr` 正确但**读数据恒为 0** | bit40 的 TDI 被 `or` 上了导航用的预设 `t6`（里面**带了 TDI=1**），于是每个请求的地址都被或上 `0x40` → 落到非法寄存器，写全不生效 |
| 高位（bit32..40）恒为 0 | 8 位累加器是"每拍右移、新位进 bit31"，**只有正好 32 位**才自然对齐；8 位会停在 bit[31:24]，要 `srli 24` 挪回来 |
| 响应整体**晚一位**（`dmstatus` 读出 `0x00400CA2<<1`） | 高电平相位 nop 不够，TDO 采样点太早。`DMI_CAP_HIGH_NOP` 是编译期可调项，扫出来 **<8 就不行** |

时序可调项（CMake cache / 环境变量覆盖，见 `CMakeLists.txt`）：
`DMI_CAP_HIGH_NOP`（采样点）、`DMI_NAV_LOW_NOP` / `DMI_NAV_HIGH_NOP`
（导航时钟低/高电平）。**注意导航时钟的低电平不能省**：TMS/TDI 是在拉低的
同一次 `DO_VAL` 写里给出的，紧跟着拉高会让目标采到旧 TMS。

### 结果（`script_test/hpm6800_riscv.py`，1024 B × 50 轮）

| 实现 | 写 | 读 |
|---|---|---|
| 慢路径（C `jtag_seq` 拼 6 次调用） | 957.4 KB/s | 954.5 KB/s |
| **专用 DMI 汇编（cap=8/nav=8）** | **1195.3 KB/s** | **1181.6 KB/s** |
| 主机驱动 OpenOCD（最好后端） | 154.0 | 95.9 |

单字 SBA 读 25.6 µs（4 次 DMI 扫描）。

**教训**：一次 DMI 访问 54 个 TCK（41 移位 + 5 导航 + 8 idle），按 360 MHz 主频、
每 bit 约 23 周期算 ≈ 3.4 µs，和实测 3.4 µs/字吻合——所以现在**瓶颈回到了
bit 循环本身**（其中 10/23 是采样 nop）。要再往上就得压采样点或减少每 bit 指令数，
而这受限于电平转换往返延迟，只能实测。有完整性自检脚本兜底：
`script_test/hpm6800_selfcheck.py`（探针写已知图案再读回比对校验和，PASS 才算数）。

---

## 5. 阶段四：狂发例程与 RTT 交付率 —— **卡在板子不启动**

固件已就绪：`script_test/hpm6800evk_rtt_flood/`（`flash_xip`，RTT 上行 32 KB，
`BLOCK_IF_FIFO_FULL`，死循环发 `hello world!\n`），
`_SEGGER_RTT` 落在 **0x01240000**（AXI SRAM，探针可直接读写）。

已确认的：

- 构建通过（注意：**本地 SDK 的 `drivers/src/hpm_enet_drv.c` 被改过，无条件
  `#include "trace_log.h"`**，而该头只存在于 `samples/lwip/lwip_tcpecho/src/TRACE_LOG/`，
  任何新工程都会因此编不过；本工程把它加进 include 路径绕过）；
- 用本探针烧写成功，**flash 内容与 `.bin` 逐字节一致**；
- 镜像头 `0100f9fc070000000000000000000000` 与用户自己那份能跑的
  `work/segger_rtt_printf_test_...` 构建**完全相同**。

**没解决的**：`reset halt` 后 `reg pc = 0x2001d4c8`，跑 300 ms 后**还是同一个值**
——CPU 停在**boot ROM**里（该地址不属于本工程任何段，`_start = 0x80003000`），
从未跳进应用；因此 0x1240000 处的 RTT 控制块里没有 `SEGGER RTT` 签名。

下一步要人工确认的（我看不到板子）：

1. **BOOT 跳线**：EVK UG 明确写了 "BOOT1=0，即调整到从 NOR FLASH 启动"，
   若 BOOT 脚选择的是串口/USB 启动，ROM 就会停在那儿等（与现象一致）；
2. 板上是否曾跑过有效镜像（`flash write_image erase` 只擦写了前 56 KB）；
3. 串口（UART0 115200）有没有输出，能直接区分"没启动"和"启动了但崩了"。

一旦能启动，探针侧 RTT 桥接 RISC-V 后端（把 `rtt_bridge.c` 里的
`swd_read_memory`/`swd_write_word` 换成 `riscv_jtag_read`/`riscv_jtag_write_word`，
按 1.19 MB/s 的实测值，交付率应当在 **1.1~1.2 MB/s** 量级，与 SWD 侧
（2.9 MB/s）同源但受 DMI 41bit/32bit 的固有开销限制）即可收尾。

---

## 6. 复现清单

```bat
:: 1) 探针固件（HPM5301EVKLite）
cd firmware\application_5301
set DMI_CAP_HIGH_NOP=8 && set DMI_NAV_LOW_NOP=8 && set DMI_NAV_HIGH_NOP=8
python ..\..\script_test\hpm6800_flash_probe.py          :: 构建 + DFU 升级
python ..\..\script_test\hpm6800_probe.py set-mode 1

:: 2) 目标调试（OpenOCD）
python script_test\hpm6800_flash_target.py               :: 用探针烧 HPM6800EVK

:: 3) 探针侧引擎
python script_test\hpm6800_riscv.py open
python script_test\hpm6800_selfcheck.py                  :: 完整性（PASS 才算数）
python script_test\hpm6800_riscv.py rbench 0x1200000 1024 50
python script_test\hpm6800_riscv.py wbench 0x1200000 1024 50

:: 4) 主机侧 SRAM 口径
python script_test\sram_speed_hpm6800.py --size 65536 --regions axi

:: 5) 时序扫描（每点一次构建+烧写+自检，约 2 分钟）
powershell -File script_test\hpm6800_timing_sweep.ps1
```

### HID 协议补充

`CMD_RISCV = 0x32`：`req[3]=action`（0 停、1 开、2 读基准、3 写基准、4 单字读基准、
5 读回校验、6 状态、7 时序、8 DMI 原始扫描），`req[4..7]=addr`、`req[8..11]=arg1`、
`req[12..13]=arg2`；响应 12 个 32 位状态字：
`[0] open|pending<<8|rc<<16|action<<24`、`[1] idcode`、`[2] dtmcs`、`[3] dmstatus`、
`[4] moved`、`[5] ticks(24MHz)`、`[6] sbcs`、`[7] delay|iters<<8`、
`[8] bytes/s`、`[9] 校验和`、`[10..11] 前两个字`。
**注意 STATUS/CONFIG 不入队**（队列只有一个槽，排队会把在跑的操作冲掉）。
