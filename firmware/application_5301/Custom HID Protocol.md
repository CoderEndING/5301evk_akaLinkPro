# Custom HID 通信协议

## HID 数据包

固定长度64字节

Byte[0x00] = Report ID
Byte[0x01] = Data Length（包括 Command type 和后面的数据长度）
Byte[0x02] = Command type
Byte[0x03-0x3F] = Command data（可选）

- 主机下发 request 使用 Report ID 0x01，设备回应 response 使用 Report ID 0x02。
- Data Length = Command type 字节数(1) + 后续有效数据字节数。

## 指令类型

1. 获取配置指令 0x01
   主机发送 request
   Byte[0x00] = 0x01 // Report ID
   Byte[0x01] = 0x01 // Data Length
   Byte[0x02] = 0x01 // Command type
   设备回应 response
   Byte[0x00] = 0x02 // Report ID
   Byte[0x01] = 0x07 // Data Length
   Byte[0x02] = 0x01 // Command type
   Byte[0x03] = 输出模式 0x00 - SWD + VCOM; 0x01 - SWD + JTAG
   Byte[0x04] = 5V输出模式 0x00 - Disable; 0x01 - Enable
   Byte[0x05] = 时钟加速模式 0x00 - Disable; 0x01 - Enable
   Byte[0x06] = LED1 显示模式 0x01 - 0x09（见配置说明）
   Byte[0x07] = LED2 显示模式 0x01 - 0x09（见配置说明）
   Byte[0x08] = 外部参考设定值 低八位（单位 mV）
   Byte[0x09] = 外部参考设定值 高八位（单位 mV，范围 1800 - 5000）
   设备回应代表成功

2. 设置配置指令 0x02
   主机发送 request
   Byte[0x00] = 0x01 // Report ID
   Byte[0x01] = 0x07 // Data Length
   Byte[0x02] = 0x02 // Command type
   Byte[0x03] = 输出模式 0x00 - SWD + VCOM; 0x01 - SWD + JTAG
   Byte[0x04] = 5V输出模式 0x00 - Disable; 0x01 - Enable
   Byte[0x05] = 时钟加速模式 0x00 - Disable; 0x01 - Enable
   Byte[0x06] = LED1 显示模式 0x01 - 0x09
   Byte[0x07] = LED2 显示模式 0x01 - 0x09
   Byte[0x08] = 外部参考设定值 低八位（单位 mV）
   Byte[0x09] = 外部参考设定值 高八位（单位 mV，范围 1800 - 5000）
   设备回应 response
   Byte[0x00] = 0x02 // Report ID
   Byte[0x01] = 0x01 // Data Length
   Byte[0x02] = 0x02 // Command type
   设备回应代表成功
   说明：本指令只写入内存并立即生效，掉电不保存；需要掉电保存请随后发送保存配置指令 0x04。
   越界的 LED 模式会被钳制为 0x09（常灭），越界的外部参考设定值会被钳制到 1800 - 5000。

3. 获取 Target 电压获取指令 0x03
   主机发送 request
   Byte[0x00] = 0x01 // Report ID
   Byte[0x01] = 0x01 // Data Length
   Byte[0x02] = 0x03 // Command type
   设备回应 response
   Byte[0x00] = 0x02 // Report ID
   Byte[0x01] = 0x03 // Data Length
   Byte[0x02] = 0x03 // Command type
   Byte[0x03] = 电压数据低八位
   Byte[0x04] = 电压数据高八位（电压单位为mV）
   设备回应代表成功
   说明：返回 PB10 分压点经 x2 还原后的外部参考电压（ADC 量程 3.3V）。

4. 保存当前配置设置指令 0x04
   主机发送 request
   Byte[0x00] = 0x01 // Report ID
   Byte[0x01] = 0x01 // Data Length
   Byte[0x02] = 0x04 // Command type
   设备回应 response
   Byte[0x00] = 0x02 // Report ID
   Byte[0x01] = 0x01 // Data Length
   Byte[0x02] = 0x04 // Command type
   设备回应代表成功
   说明：将当前配置写入 flash 持久化保存，见“配置持久化”章节。

5. 获取型号指令 0x10
   主机发送 request
   Byte[0x00] = 0x01 // Report ID
   Byte[0x01] = 0x01 // Data Length
   Byte[0x02] = 0x10 // Command type
   设备回应 response
   Byte[0x00] = 0x02 // Report ID
   Byte[0x01] = 0x13 // Data Length
   Byte[0x02] = 0x10 // Command type
   Byte[0x03] = 'a'
   Byte[0x04] = 'k'
   Byte[0x05] = 'a'
   Byte[0x06] = 'L'
   Byte[0x07] = 'i'
   Byte[0x08] = 'n'
   Byte[0x09] = 'k'
   Byte[0x0A] = ' '
   Byte[0x0B] = 'C'
   Byte[0x0C] = 'M'
   Byte[0x0D] = 'S'
   Byte[0x0E] = 'I'
   Byte[0x0F] = 'S'
   Byte[0x10] = '-'
   Byte[0x11] = 'D'
   Byte[0x12] = 'A'
   Byte[0x13] = 'P'
   Byte[0x14] = \0
   设备回应代表成功

6. 获取序列号指令 0x11
   主机发送 request
   Byte[0x00] = 0x01 // Report ID
   Byte[0x01] = 0x01 // Data Length
   Byte[0x02] = 0x11 // Command type
   设备回应 response
   Byte[0x00] = 0x02 // Report ID
   Byte[0x01] = 0x0E // Data Length
   Byte[0x02] = 0x11 // Command type
   Byte[0x03] = SN 第 1 位 ACSII
   Byte[0x04] = SN 第 2 位 ACSII
   Byte[0x05] = SN 第 3 位 ACSII
   Byte[0x06] = SN 第 4 位 ACSII
   Byte[0x07] = SN 第 5 位 ACSII
   Byte[0x08] = SN 第 6 位 ACSII
   Byte[0x09] = SN 第 7 位 ACSII
   Byte[0x0A] = SN 第 8 位 ACSII
   Byte[0x0B] = SN 第 9 位 ACSII
   Byte[0x0C] = SN 第 10 位 ACSII
   Byte[0x0D] = SN 第 11 位 ACSII
   Byte[0x0E] = SN 第 12 位 ACSII
   Byte[0x0F] = \0
   设备回应代表成功

7. 获取硬件版本号 0x12
   主机发送 request
   Byte[0x00] = 0x01 // Report ID
   Byte[0x01] = 0x01 // Data Length
   Byte[0x02] = 0x12 // Command type
   设备回应 response
   Byte[0x00] = 0x02 // Report ID
   Byte[0x01] = 0x06 // Data Length
   Byte[0x02] = 0x12 // Command type
   Byte[0x03] = HW 第 1 位 ACSII
   Byte[0x04] = HW 第 2 位 ACSII
   Byte[0x05] = HW 第 3 位 ACSII
   Byte[0x06] = HW 第 4 位 ACSII
   Byte[0x07] = \0
   设备回应代表成功

8. 获取固件版本号 0x13
   主机发送 request
   Byte[0x00] = 0x01 // Report ID
   Byte[0x01] = 0x01 // Data Length
   Byte[0x02] = 0x13 // Command type
   设备回应 response
   Byte[0x00] = 0x02 // Report ID
   Byte[0x01] = 0x06 // Data Length
   Byte[0x02] = 0x13 // Command type
   Byte[0x03] = FW 第 1 位 ACSII
   Byte[0x04] = FW 第 2 位 ACSII
   Byte[0x05] = FW 第 3 位 ACSII
   Byte[0x06] = FW 第 4 位 ACSII
   Byte[0x07] = \0
   设备回应代表成功

9. 获取Bootloader版本号 0x14
   主机发送 request
   Byte[0x00] = 0x01 // Report ID
   Byte[0x01] = 0x01 // Data Length
   Byte[0x02] = 0x14 // Command type
   设备回应 response
   Byte[0x00] = 0x02 // Report ID
   Byte[0x01] = 0x06 // Data Length
   Byte[0x02] = 0x14 // Command type
   Byte[0x03] = BL 第 1 位 ACSII
   Byte[0x04] = BL 第 2 位 ACSII
   Byte[0x05] = BL 第 3 位 ACSII
   Byte[0x06] = BL 第 4 位 ACSII
   Byte[0x07] = \0
   设备回应代表成功

10. 获取硬件生产日期指令 0x15
   主机发送 request
   Byte[0x00] = 0x01 // Report ID
   Byte[0x01] = 0x01 // Data Length
   Byte[0x02] = 0x15 // Command type
   设备回应 response
   Byte[0x00] = 0x02 // Report ID
   Byte[0x01] = 0x15 // Data Length
   Byte[0x02] = 0x15 // Command type
   Byte[0x03-0x15] = 日期字符串（19 字节，如 "2026-08-06"）
   Byte[0x16] = \0
   设备回应代表成功

11. 获取固件编译日期指令 0x16
   主机发送 request
   Byte[0x00] = 0x01 // Report ID
   Byte[0x01] = 0x01 // Data Length
   Byte[0x02] = 0x16 // Command type
   设备回应 response
   Byte[0x00] = 0x02 // Report ID
   Byte[0x01] = 0x15 // Data Length
   Byte[0x02] = 0x16 // Command type
   Byte[0x03-0x15] = 日期字符串（19 字节）
   Byte[0x16] = \0
   设备回应代表成功

12. 获取Bootloader编译日期指令 0x17
   主机发送 request
   Byte[0x00] = 0x01 // Report ID
   Byte[0x01] = 0x01 // Data Length
   Byte[0x02] = 0x17 // Command type
   设备回应 response
   Byte[0x00] = 0x02 // Report ID
   Byte[0x01] = 0x15 // Data Length
   Byte[0x02] = 0x17 // Command type
   Byte[0x03-0x15] = 日期字符串（19 字节）
   Byte[0x16] = \0
   设备回应代表成功

13. 探针侧 RTT 桥指令 0x31
    主机发送 request
    Byte[0x00] = 0x01 // Report ID
    Byte[0x01] = 0x0D // Data Length（START 时；简单查询用 1）
    Byte[0x02] = 0x31 // Command type
    Byte[0x03] = action：0=停止，1=启动，2=查状态，3=自动启动（自己搜控制块），
                  4=原始 DAP 透传（调试用），5=读探针自身内存（调试用），6=取透传结果，
                  7=运行时调参，8=纯 SWD 基准，9=取基准结果，
                  **10=切换目标类型**（Byte[0x04]：0=SWD/ARM，1=RISC-V/JTAG，
                  见下方第 16 条）
    Byte[0x04-0x07] = 目标地址（action=1 时可选，0 = 用默认搜索区间；
                                action=8 时为目标地址）
    Byte[0x08-0x0B] = 搜索长度（action=1 时可选，0 = 默认；
                                action=8 时 low16 = 每次读的字节数，high16 = 轮数）
    Byte[0x0C] = RTT 通道号（默认 0）
    action=7 调参：Byte[0x04-0x07] = SWD 时钟 Hz（0 = 不改，运行中也能改），
                   Byte[0x08-0x09] = 块读字节数（0 = 不改，钳到 64~4096），
                   Byte[0x0A] = bit0 丢弃模式（搬走但不送 CDC，用于量纯 SWD 侧），
                   Byte[0x0B] = clock_delay 覆盖（0xFF = 用档位原生值；实测无差别）
    设备回应 response
    Byte[0x00] = 0x02 // Report ID
    Byte[0x01] = 0x32 // Data Length = 1(command) + 1(rc) + 48(12 个状态字)
    Byte[0x02] = 0x31 // Command type
    Byte[0x03] = 返回码（0 = 正常；启动失败为负：-1=SWJ_Clock 失败，-2=SWD 初始化
                 失败，-3=没找到 RTT 控制块）
    Byte[0x04..0x33] = 12 个 32 位小端状态字（见下表）

    状态字（action=2/1/3 时为桥的状态；action=5 时前若干字是读回的探针内存，
    action=6 时前若干字是透传回来的 DAP 响应字节）：

    | 字 | 位域 | 含义 |
    | --- | --- | --- |
    | [0] | bit0 / bit8-15 / bit16 / bit24-31 | 运行中 / 通道 / SWD 已就绪 / clock_delay |
    | [1] | 32 位 | 找到的 RTT 控制块地址（`SEGGER RTT` 签名处） |
    | [2] | 32 位 | 上行缓冲描述符地址 |
    | [3] | 32 位 | 已搬运字节数（每次启动清零） |
    | [4] | 低 16 / 高 16 | 轮询次数 / 实际搬运次数 |
    | [5] | 低 16 / 高 16 | 目标内存读错误数 / RdOff 写错误数 |
    | [6] | 低 16 / 高 16 | 上次搬运字节数 / 空环（无数据可搬）次数 |
    | [7] | 低 16 / 高 16 | 因 DAP 忙而让路的次数 / 重新扫描控制块次数 |
    | [8] | 低 8 | 最近一条 DAP 命令 ID |
    | [9] | 32 位 | 最近一条 DAP 响应 |
    | [10] | 低 8 | 最近一次启动的返回码 |
    | [11] | 低 16 / bit16 / 高 8 | 块读字节数 / 丢弃模式 / 当前 SWD 档位（MHz） |

    action=8/9（纯 SWD 基准）：8 只排队，结果由 9 取回，响应前 3 个字为
    [0]=错误码（0 正常，-1 SWD 初始化失败，-2 读失败）、[1]=总字节数、
    [2]=耗时 MCHTMR tick（24 MHz）。

    说明：桥在探针固件里自己完成 RTT 控制块搜索、环形缓冲搬运与 RdOff 回写，数据
    直接进 CDC 串口环（此时 UART 侧不再写同一个环）。SWD 访问用 ARM DAPLink 官方
    `swd_host.c`（`src/swd_host/`）。它只在 DAP 空闲 ≥20 ms 时轮询，正常调试会话最多
    多约 1 ms 抖动。实测 2.14 MB/s 且零丢包，测速脚本 `script_test/rtt_probe_bridge.py`。

    **目标可以是 RISC-V**（action 10 切换）：桥自身的逻辑（找控制块、搬环形缓冲、
    回写 RdOff）两边完全一样，只有底下三个原语分派不同 —— 读/RdOff 写走
    `src/riscv/` 的 DMI+SBA 引擎，初始化走 `riscv_jtag_open()`（TAP 复位 + 加载
    `IR=0x11` + 唤醒 DM）。此时 action 7 的"SWD 时钟 Hz"对 RISC-V 无意义，只有
    <256 的值会被当成 DMI 的 idle 周期数（默认 8，见第 16 条）。
    实测 HPM6800EVK 交付 **1105~1165 KB/s 且字节级零丢包**，
    测速脚本 `script_test/hpm6800_rtt_delivery.py` / `hpm6800_rtt_loss.py`。

14. 设备复位指令 0xFE
   主机发送 request
   Byte[0x00] = 0x01 // Report ID
   Byte[0x01] = 0x01 // Data Length
   Byte[0x02] = 0xFE // Command type
   设备复位，不会回复，此时连接断开

15. 进入DFU模式设置指令 0xFF
   主机发送 request
   Byte[0x00] = 0x01 // Report ID
   Byte[0x01] = 0x01 // Data Length
   Byte[0x02] = 0xFF // Command type
   设备复位进入DFU模式，不会回复，此时连接断开

16. 探针侧 HSS 采样指令 0x32（J-Scope 波形页的数据源）
    探针自己按你设的周期，用 SWD 读目标 RAM 里 1~8 个变量（地址来自目标 .elf 的 DWARF），
    组 512 B 自描述包，从 **interface 0 上原本闲置的 bulk IN `0x83`** 推给主机。
    目标固件一行都不用改。

    控制面（本命令）与数据面（0x83）的字节布局**必须与网页逐字节一致** —— 权威实现是
    另一仓库 web-serial-rtt-tools 的 `app/scope/protocol.js` / `view.js`。

    ⚠️ 网页的 `xfer()` 回来时**已经剥掉 Report ID**（`res[1] === cmd`），所以
       网页的 `res[i]` == 固件里的 `res_hid[i + 1]`。下面按**固件下标**写。

    主机发送 request
    Byte[0x00] = 0x01 // Report ID
    Byte[0x01] = 长度
    Byte[0x02] = 0x32 // Command type
    Byte[0x03] = action
    Byte[0x04-0x07] = 参数（周期 us / SWD Hz / 标定轮数）
    Byte[0x08-0x09] = flags / nvars，变量表从 Byte[0x0A] 起，每个 6 B（addr u32 + size u8 + type u8）

    | action | 含义 | 数据段 |
    | --- | --- | --- |
    | 0 | 停止 | — |
    | 1 | 启动推流（会先 `rtt_bridge_stop()`，两者互斥） | — |
    | 2 | 查状态 | — |
    | 3 | 设 SWD 时钟 | `hz(4)`；0 = 不动。走 RTT 桥那套斜坡换挡 |
    | 4 | 触发配置（v2，当前只回 OK） | — |
    | 7 | 配置：周期 + 1~8 个变量 | `period_us(4) flags(1) nvars(1) n×(addr4,size1,type1)`，8 个变量 = 55 B |
    | 8 | 标定：用当前计划空跑 N 次 | `iters(4)`，结果走 action 9 |
    | 9 | 取标定结果 | → 见下 |

    `period_us` 范围 **2 ~ 1000000**（低于 2 会被钳到 2）。这个下限是一路放下来的：
    5（"一次读 4.478 µs 做不完"）→ 3（单字快路径 2.681 µs）→ 2（单字流水读 1.589 µs，
    每拍只 1 次传输）。**周期是整数微秒**，所以 2 就是下限；拐点落在 2 与 3 µs 之间
    （3 µs 零丢、2 µs 丢 ~5%），想吃到中间值得改协议的时间轴单位。

    单变量 u32 的快路径（**只有一个 span** 且它是 4 字节直读时自动启用，主机不用管）：
    ① CSW 切成 `AddrInc=0` + 缓存 AP.TAR，地址不变就跳过 TAR 写 ⇒ 每拍 2 次传输
    （DRW + RDBUFF）；② 再把它当流水线，每拍只发 1 次 DRW 读、返回值回填上一拍的槽位
    ⇒ 每拍 1 次传输。实测 M0 4.503 → 2.681 → **1.589 µs**（222 → 373 → 629 kHz），
    端到端 3 µs 周期 ~329 kHz（探针侧零丢，余下是主机排空）。
    多 span 或含多字 span 的配置自动退回原路径 —— **不要**把守卫放宽成"所有 span 都是
    单字"：两个远离的单字 span 交替读时每拍都要重写 TAR，那条路的 TAR 写比裸写还贵，
    实测会 -31%。

    type：`0=u8 1=i8 2=u16 3=i16 4=u32 5=i32 6=f32 7=f64`
    flags：bit0 允许 60 MHz；bit1 丢弃模式；bit2 触发；bit3 不让路（独占链路）；
           bit4 SWD 空闲拍压到 0（`DAP_Data.clock_delay=0`）；
           bit5 采样期间自动暂停 CDC/串口桥（停采样自动恢复，见第 18 条）

    响应：Byte[0x01] = 长度，Byte[0x02] = 0x32，**Byte[0x03] = 启动码**（网页读 `res[2]`，
    -100 = 排队中，0 = 正常，-1/-2/-3/-4 见 scopeRcText），Byte[0x04..0x33] = 12 个状态字。

    action=9 时前 3 个字换成标定结果：**Byte[0x04..07] = ticks（24 MHz）、
    Byte[0x08..0B] = iters、Byte[0x0C..0F] = err**（网页读 `res[3]` / `res[7]`）。
    每样本真实耗时 = ticks / 24 / iters（µs）。

    标定响应里字 3/4 平时用不到，顺手拿来回报**实际装载了哪个 SWD blob** ——
    `Byte[0x10..13] = Read_GPIO_ASM 在 swd_ops 里的偏移`、`Byte[0x14..17] = clock_delay`。
    没有这个数就分不出"时钟命令被忽略"和"生效了但没差别"。踩过的坑：`SWJ_Clock` 在
    桥那一侧链路没就绪时只把值记下来、不装载 blob，而采样器有自己一份链路状态，
    于是 1 MHz 与 60 MHz 的标定读数一模一样、状态字 w1 却报着新频率；从慢档跳回快档
    时还会因为少了那段 20 MHz 斜坡而在第一次访问就 -4。现在采样器改走
    `rtt_bridge_request_swd_clock()`（换挡一律"下次重新初始化"）。偏移对照表：
    `0x53C=60M(6 指令/bit) 0x60C=45M(8) 0x6E0=36M(10) 0x7C4=30M(12) 0xA54=20M(18)
     0x620=SLOW  0xFFFFFFFF=还没装载过`。

    状态字：w0 = running | spans<<8 | swdReady<<16 | nvars<<24；w1 = 实际 SWD Hz；
    w2 = 采到的样本数；w3 = 丢样本数（跳拍 + 无缓冲）；w4 = 已推字节低 16 / 无缓冲丢样本高 16；
    w5 = SWD 读错低 16 / 让路次数高 16；w6 = 最近一包 seq；w7 = 跳拍低 16 / 丢弃模式包数高 16；
    w8 = 计划哈希（与网页 planHash 同算法，防"配置没生效却在画图"）；
    w9 = lastCmd | lastRsp<<8 | tx完成回调次数<<16；w10 = startRc；w11 = period | 丢弃位<<16 | MHz<<24。

    数据面（0x83，512 B 定长自描述包）：
    偏移 0 magic 'JS'(0x4A53) / 2 ver=1 / 3 kind / 4 seq(4) / 8 t_us(4) / 12 n(2) / 14 aux(2) / 16 载荷 496 B
      kind 1=DEF：`swd_hz(4) period_us(4) flags(2) nvars(1) spans(1)` + n×(addr4,size1,type1)
      kind 2=DATA：载荷 = n 帧，变量按**地址排序后**紧排、各按自己的 size 小端
      kind 3=STAT：`produced(4) dropped(4) pkts(4) usb_err(2) swd_err(2) period_actual(4) swd_mhz(1) disc(1)`，每 64 包插一个
    丢包判定：seq 跳号 / t_us 跳变 / STAT.dropped，三者都要显示，绝不静默。

    收尾顺序（WebUSB 没有取消接口）：**先 HID STOP → 等 100~200 ms → 把在飞的读收干净 → 再 close**。
17. 探针侧 RISC-V 引擎指令 0x33（原 0x32，让位给上面的 SCOPE）（JTAG-only 目标，如 HPM6800EVK / HPM6880）
    探针自带一套 RISC-V Debug Module 引擎：加载一次 `IR=0x11` 之后，一次 DMI 访问
    就是一次 41 位 DR 扫描（`{op[1:0], data[31:0], addr[6:0]}`），响应滞后一拍，
    所以连续 posted 请求可以一个字一次扫描地流水；块搬运走 DM 的 SBA（硬件自增地址）。
    JTAG 位翻转不能在 USB 中断里跑，所以动作都是**排队**的、由主循环的
    `riscv_svc_poll()` 执行；回复里带的是**排队那一刻**的状态块，所以查询结果要
    轮询 `action=6`（或看 bit8 的 pending 位变 0）。

    主机发送 request
    Byte[0x00] = 0x01 // Report ID
    Byte[0x01] = 0x0E // Data Length（最多 1+1+4+4+4+2）
    Byte[0x02] = 0x32 // Command type
    Byte[0x03] = action
    Byte[0x04-0x07] = 目标地址
    Byte[0x08-0x0B] = 参数 1
    Byte[0x0C-0x0D] = 参数 2（只有 16 位）

    | action | 含义 | 参数 |
    | --- | --- | --- |
    | 0 | `stop`：释放端口（引脚回空闲） | — |
    | 1 | `open`：TAP 复位 + 加载 `IR=DMI` + 置 `dmcontrol.dmactive`。**基准类动作都要求先 open** | — |
    | 2 | `rbench`：SBA 块读基准 | addr / 参数1 = 字节数（钳到 ≤2048）/ 参数2 = 轮数（≤64） |
    | 3 | `wbench`：SBA 块写基准 | 同上 |
    | 4 | `sbench`：单字 SBA 读基准（每字 4 次 DMI 扫描） | addr / 参数1 = 轮数 |
    | 5 | `rcheck`：写已知图案（`k*7+3`）再读回比对校验和 —— **完整性门禁** | addr = 基值 / 参数1 = 字数 |
    | 6 | 查状态，**不排队**（也是所有动作的默认查询口） | — |
    | 7 | `config`：设 DMI idle 周期数，**立即生效、不排队** | addr = 周期数 |
    | 8 | `dmiprobe`：原始 DMI 扫描（TAP 复位 → `IR=DMI` → 4 次请求），把原始 41 位响应塞进状态块 | addr = 扫描条数（0 = 6） |

    设备回应 response
    Byte[0x00] = 0x02 // Report ID
    Byte[0x01] = 0x32 // Data Length = 1(回显 action) + 1 + 48(12 个状态字)
    Byte[0x02] = 0x32 // Command type
    Byte[0x03] = **回显 action**（注意不是返回码，返回码在状态字 [0] 的 bit16-23）
    Byte[0x04..0x33] = 12 个 32 位小端状态字

    | 字 | 位域 | 含义 |
    | --- | --- | --- |
    | [0] | bit0 / bit8 / bit16-23 / bit24-31 | 端口已打开 / 有动作在排队或执行中 / 返回码（见下）/ 最近动作号 |
    | [1] | 32 位 | TAP IDCODE（HPM6880 = `0x1000563D`） |
    | [2] | 32 位 | DTMCS（HPM6880 = `0x00007071`，其 `idle` 字段为 7） |
    | [3] | 32 位 | DMSTATUS（HPM6880 = `0x00400CA2`） |
    | [4] | 32 位 | 本次动作搬运的字节数 |
    | [5] | 32 位 | 耗时 MCHTMR tick（24 MHz） |
    | [6] | 32 位 | 最近一次读到的 SBCS（出错时看 `sbbusyerror` / `sberror`） |
    | [7] | 低 8 / 高 24 | 当前 DMI idle 周期数 / 完成的轮数 |
    | [8] | 32 位 | 速率（字节/秒） |
    | [9] | 32 位 | 校验和（action=5） |
    | [10..11] | 32 位 ×2 | 校验和读回的前两个字（action=5） |

    返回码（状态字 [0] 的 bit16-23）：`0` = 正常；`1` = 目标端口没打开
    （基准/校验类动作在没 `open` 时会直接返回这个，**很容易误判成"引擎坏了"**）；
    `0xFF`（即 -1 截断）= 读失败；action=1 时是失败原因 `1/2/3`。

    **action=8 是个例外**：状态字 [4..11] 被替换成 4 次原始 41 位扫描，
    每次占两个字（低 32 位 + 高 9 位），看 `op[1:0]` / `data` / `addr` 用来判断
    "DMI 应答了吗、应答的是哪个寄存器"。

    说明与约束：
    - **探针必须先切到 `output_mode=1`（SWD+JTAG）**，`output_mode=0` 会拒绝 JTAG；
      该设置只在 RAM 里，探针复位/重插即丢。见第 9 条 `CMD_SET_CONFIG`。
    - `idle` 周期数**不能减**：HPM6880 的 DTM `dtmcs.idle` 读出来是 7，实测 8 稳、
      6 只跑得动 6/50 轮就死、≤4 立刻不应答。HPM6800EVK 侧 JTAG TCK 规格上限
      25 MHz，当前引擎跑在 ~16.4 MHz（一次 DMI 访问 54 TCK）。
    - action 2/3/5 会**直接读写目标内存**，别指向目标正在用的区域。
    - 完整背景、接线坑与失败实验见 `docs/hpm6800evk-jtag.md`。
18. 主循环 CDC/串口桥开关指令 0x34（网页面板用）
    探针主循环每轮都要服务 CDC/串口桥（VCOM 转发 + RTT-over-USB 转发）：一次读 DMA
    的 `DSTADDR`、两次关中断、三次环形缓冲查询。**这几百个 CPU 周期正是高频 J-Scope
    采样时缺的那一块** —— 单变量 u32、5 µs 周期下关掉它，端到端从 ~180 kHz 提到
    **~197 kHz**，丢包从 10.6% 降到 1.9%。采样数据走的是另一条 bulk IN `0x83`，与
    本开关无关；代价只是**暂停期间 COM 口不通**（CDC 的 bulk OUT 被 NAK，主机自行重试）、
    RTT-over-USB 也停 —— 但采样器与 RTT 桥本来就互斥（第 16 条的启动分支会互相 stop）。

    主机发送 request
    Byte[0x00] = 0x01 // Report ID
    Byte[0x01] = 0x02 // Data Length = action(1) + 参数(1)
    Byte[0x02] = 0x34 // Command type
    Byte[0x03] = action（0 = 查状态，1 = 设置）
    Byte[0x04] = 参数（action=1 时：0 = 关桥，1 = 开桥）

    设备回应 response
    Byte[0x00] = 0x02 // Report ID
    Byte[0x01] = 0x06 // Data Length = 1(回显 action) + 1 + 4
    Byte[0x02] = 0x34 // Command type
    Byte[0x03] = **回显 action**
    Byte[0x04..0x07] = 状态字（小端）：bit0 = 桥当前是开的；bit8 = 本命令被支持（探活用）

    说明与约束：
    - **默认是开的**（bit0 = 1）。开→关再开时，探针会把暂停期间积压的串口数据丢掉并把
      DMA 定位追平（`uartx_rx_resync()`），不会在恢复瞬间灌一整圈陈旧字节给主机。
    - 状态**不持久化**，探针复位/重插即恢复为开。
    - 第 16 条的 `SCOPE` flags **bit5** 是"采样期间自动关、停采样自动恢复"，一般用那个
      就够了（网页只要在已有的 CONFIG 报文里加一位，不必自己管状态）；本命令是给面板
      做**显式勾选框**用的。自动暂停只恢复"自己关过的那一次"，不会覆盖手动关掉的状态。
    - 关着的时候别去开 RTT —— 桥被关了，RTT 的数据没有出口。

## 配置说明

1. 输出模式
0x00 - SWD + VCOM 此时 JTAG 功能不可用，TDI 和 TDO 用于 UART
0x01 - SWD + JTAG 此时 VCOM 功能不可用，TDI 和 TDO 用于 JTAG

2. 5V输出模式
0x00 - 关闭 5V 对外输出
0x01 - 开启 5V 对外输出，可以用于为外接隔离器模块或目标板供电
注意：该引脚同时给板载电平转换供电，关闭后 JTAG/SWD 电平转换可能无法工作；出厂默认开启。

3. 时钟加速模式
0x00 - 关闭时钟加速模式，此时 DAP_SWJ_Clock 指令将按照设定频率向下取整设置 SWD 和 JTAG 输出频率，适用于 openocd 等上位机
0x01 - 开启时钟加速模式，此时 DAP_SWJ_Clock 指令将按照设定频率x10向下取整设置 SWD 和 JTAG 输出频率，适用于 Keil MDK 上位机

4. LED 显示模式（LED1 = PB11 蓝色，LED2 = PB12 黄色，高电平点亮）
可分别为两个 LED 配置 0x01 - 0x09 中任意一种模式，两个 LED 可相同也可不同：

0x01 - DAP RUNNING 状态：RUNNING=1 则亮，=0 则灭
0x02 - DAP CONNECT 状态：CONNECT=1 则亮，=0 则灭
0x03 - DAP 状态：RUNNING 与 CONNECT 任一为 1 则以 5Hz（200ms 周期，亮/灭各 100ms）闪烁，两者都为 0 则常亮
0x04 - 调试器电源状态：上电常亮
0x05 - 外部参考电源状态：ADC 检测到外部参考电压高于设定值的 90% 时点亮，低于 85% 时熄灭（5% 滞回）
0x06 - CDC 串口 TX 状态（调试器 UART 向外发数据）：有数据发送时点亮，最低点亮 50ms，持续发送则常亮
0x07 - CDC 串口 RX 状态（调试器 UART 接收数据）：有数据接收时点亮，最低点亮 50ms，持续接收则常亮
0x08 - CDC 串口 TX 或 RX 状态：TX 或 RX 任一触发，逻辑同上
0x09 - 常灭

出厂默认：LED1 = 0x04，LED2 = 0x05。

5. 外部参考电压设定值（单位 mV，范围 1800 - 5000）
用于 LED 显示模式 0x05 的判定阈值，对应经 x2 还原后的外部参考电压。出厂默认 3300mV。

## 配置持久化

- 设置配置指令（0x02）只修改内存中的配置并立即生效，掉电丢失。
- 保存配置指令（0x04）通过 **EasyFlash（ENV, NG 模式）** 写入 QSPI NOR flash，
  具备磨损平衡与掉电保护。
- 存储位置：APP 固件区尾部保留的两个 4K 扇区（`0x800FE000` 与 `0x800FF000`），
  不占用 Bootloader 区（`0x80000000` - `0x8001FFFF`）。
- ENV key 为 `"cfg"`，value 为整个 `api_param_t`；上电自动加载，校验失败则写回出厂默认。
- 常规 APP 升级（J-Link 或 dfu-util）不会覆盖该区域。
- 详见 `Flash_Memory_Map.md` 与 `Firmware_Integrity_Plan.md`。

## 固件元数据（版本 / CRC）

- 版本、编译时间、描述、硬件版本等由构建后 `firmware/tools/pack.py` 注入：
  - **APP**：`0x80020000` 起 256 B 头（签名 + 长度 + CRC32 + 版本 + 编译时间 + 描述），代码入口 `0x80020100`；
  - **Bootloader**：`0x8001F000` 起 256 B 信息块（版本 + 编译时间 + 硬件版本 + 生产日期）。
- 版本单一来源：`firmware/version.json`；编译时间取打包时刻。
- Bootloader 启动时校验 APP 头（签名 + 长度 + CRC32），失败则停留在 DFU 模式。
- 指令 `0x12`–`0x17` 即从上述固定地址读取返回。

## WebUI 配置界面

点击连接按钮尝试连接 HID 设备
弹出 HID 设备选择框，选择 akaLink CMSIS-DAP 设备
进行连接，如果连接失败显示错误提示信息
连接成功，读取型号，序列号，配置信息，并将数据显示在页面上
此时可以修改配置，修改完配置不立即写入
点击保存配置按钮，将配置写入并保存
点击复位按钮，进行复位操作
点击进入DFU模式按钮，进入DFU模式

需要实时监测 HID 设备的连接状态，如果丢失连接要及时响应，清除显示的数据
在未连接状态下，只有连接按钮可用，其他按钮不可用，配置项目不可选

每个配置项和按键，当鼠标指上去的时候需要有提示信息
