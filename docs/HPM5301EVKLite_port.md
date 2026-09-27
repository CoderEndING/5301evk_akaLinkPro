# akaLinkPro 固件移植：HPM5301EVKLite

本文档说明如何把 akaLinkPro（基于 HPM5301 的高性能 CMSIS-DAP 调试器）固件
运行在 **HPM5301EVKLite** 官方开发板上。DAP 的目标侧输出全部走板上
**J5（20 针标准 JTAG 座）**，同一个 J5 座在固件未运行（DFU/ISP 模式）时
仍然是芯片自身的 JTAG 调试口——即这块板子**既能被调试，又能当调试器**。

- 原 akaLinkPro 板级（`boards/akaLinkPro/`）完整保留，随时可切回。
- 新增板级：`firmware/application_5301/boards/hpm5301evklite/`、
  `firmware/bootloader_dfu/boards/hpm5301evklite/`。

---

## 1. 引脚映射

### 1.1 DAP 目标侧（J5 20 针 JTAG 座）

| DAP 信号 | HPM5301 引脚 | J5 位置 | 说明 |
| --- | --- | --- | --- |
| SWCLK / TCK | **PA06** | J5.9 | FGPIO 位带输出 |
| SWDIO / TMS | **PA07** | J5.7 | 单脚双向（FGPIO 方向切换） |
| TDI | **PA05** | J5.5 | JTAG 模式输出 |
| TDO | **PA04** | J5.13 | JTAG 模式输入 |
| nRESET（目标复位） | **PA08** | J5.3（nTRST 位） | 直出低有效复位 |
| GND | — | J5.4/6/8/… | 逻辑地 |
| VTref | — | J5.1 | 板上 3.3V |
| nSRST | — | J5.15 | **接芯片自身 RESET_N，勿接目标复位**（见 §5） |

> 调试 SWD 目标最少接 3 根线：`J5.9 → SWCLK`、`J5.7 → SWDIO`、`GND`；
> 建议再加 `J5.3 → 目标 nRESET`。JTAG 模式四线全可用（TCK/TMS/TDI/TDO）。

### 1.2 板载资源

| 功能 | 引脚 | 板上位置 | 说明 |
| --- | --- | --- | --- |
| CDC 虚拟串口（UART3） | PB15 (TXD) / PB14 (RXD) | J3.8 / J3.10 | 独立引脚，即板上丝印 UART_TXD/UART_RXD；不与 TDI/TDO 复用 |
| USER KEY | PA03 | 板载按键 | 按下 = 高；**上电时按住进 ROM ISP** |
| 运行时长按 USER KEY | PA03 | — | 长按 1s → 复位进 DFU 升级模式 |
| 状态 LED | PA10 | 板载 LED2 | 低电平点亮；LED1/LED2 模式驱动同一颗灯 |
| USB0 OTG（DAP 上行口） | PA24/PA25 | J1 Type-C | USB 2.0 高速设备 |
| 调试串口 UART0 | PA00 / PA01 | J3.36 / J3.38 | console，与原板相同 |

### 1.3 与原 akaLinkPro 硬件的差异

| 项目 | akaLinkPro | HPM5301EVKLite |
| --- | --- | --- |
| SWDIO | PA28 读 + PA29 写 + PA30 方向（电平转换） | PA07 单脚双向，无方向脚 |
| nRESET | PA26 经三极管反相驱动（固件极性取反） | PA08 直出（真低有效） |
| VCOM 串口 | PA08/PA09 与 TDI/TDO 复用（JTAG 模式下串口断开） | PB15/PB14（UART3）独立，JTAG 模式下串口照常 |
| 5V 电平转换 / VREF ADC 检测 / SEL 硬件识别 | 有（PB13/PB10/PB08/09） | 无（功能自动裁剪） |
| LED | PB11+PB12 两颗，高电平点亮 | PA10 一颗，低电平点亮 |
| DFU 按键 | 专用 DFU 键 | USER KEY 长按（运行时） |
| DAP 输出座 | 板载排针 | J5 JTAG 座 |

固件通过 `board.h` 中的特性宏适配（`BOARD_HAS_SWDIO_DIR`、
`BOARD_NRESET_ACTIVE_LOW`、`BOARD_UART2_SHARES_JTAG_PINS`、
`BOARD_HAS_VREF_ADC`、`BOARD_LED_ACTIVE_LOW`、`BOARD_SWD_BLOB_EVKLITE` 等），
两个板级共用同一套应用源码。

---

## 2. 构建

本机脚本默认使用 `E:\sdk_env_v1.11.0`（SDK 1.11.0 + rv32imac 工具链），
可用环境变量 `HPM_SDK_ENV_DIR` 覆盖：

```bat
:: Bootloader (flash_xip @0x80000000) -> build_xip_evklite\
firmware\bootloader_dfu\build_xip_evklite.bat

:: APP (DFU 布局 @0x80020000) -> build_dfu_evklite\   (生成 _pack.hex/_pack.bin)
firmware\application_5301\build_dfu_evklite.bat
```

> **SDK 1.11+ 兼容说明**：`flash_dfu` 构建类型已被移除。evklite 脚本改用
> `HPM_BUILD_TYPE=flash_xip` + 自定义链接脚本 `linker/flash_dfu_app.ld`，
> 并由 CMake 追加 `-DFLASH_XIP=0 -DFLASH_DFU=1`，得到与原 `flash_dfu`
> 完全一致的镜像布局（APP 头在 0x80020000，入口 0x80020100）。
> bootloader 的 DFU 闪存移植层同时实现了新旧两代 CherryUSB 钩子
> （`dfu_write_flash`/`dfu_leave` 与 `usbd_dfu_write`/`usbd_dfu_reset`），
> 新旧 SDK 均可编译。

SWD 位带引擎是预汇编机器码 blob（内嵌 GPIO 位号）。EVKLite 引脚
（SWCLK=PA06/SWDIO=PA07）的 blob 已生成于
`src/dap/SW_DP/swd_blob_evklite.h`；如需再生成：

```
cd firmware\application_5301\src\dap\SW_DP\swd_blob
python build.py --variant 60M --toolchain <riscv-gcc-bin> ^
    --define SWD_PIN_SWCLK_SHIFT=6 --define SWD_PIN_SWDIO_SHIFT=7
:: 其余变体：45M 36M 30M 20M SLOW
```

JTAG 位带汇编直接编译自 `JTAG_DP_GPIO_ASM_45M.S`，引脚由 CMake 传入
`-DJTAG_PIN_JTCK_SHIFT=6 -DJTAG_PIN_JTMS_SHIFT=7 -DJTAG_PIN_JTDI_SHIFT=5
-DJTAG_PIN_JTDO_SHIFT=4`（默认值为 akaLinkPro 映射）。

---

## 3. 烧录

### 3.1 首次烧录（空片）：J-Link → J5

用 J-Link 的 20 针 JTAG 排线接 EVKLite 的 J5（板载 FT2232 不参与）：

```bat
firmware\application_5301\flash_jlink_evklite_full.bat
```

一次会话内烧写 bootloader + APP 并复位运行。脚本内定位于
`JLink_V882`，其它版本可用 `set JLINK_EXE=<路径>` 覆盖。

### 3.2 日常升级

- **DFU**：`build_dfu_evklite.bat` → `program_evklite.bat`（dfu-util，
  需自行安装并加入 PATH），或 bootloader 模式下把 `akaLinkPro_App_pack.bin`
  拖入虚拟 U 盘。
- **J-Link**：`flash_jlink_evklite.bat`（只烧 APP，保留 bootloader）。
- **USB ISP（救砖）**：按住 USER KEY 复位（或上电）→ ROM ISP 模式 →
  用 [hpm_manufacturing_tool](https://github.com/hpmicro/hpm_manufacturing_tool)
  经 USB/UART0 下载。

### 3.3 DFU/升级模式的进入方式

| 方式 | 操作 |
| --- | --- |
| 运行时长按 USER KEY | 1s 后自动复位进 DFU（推荐） |
| dfu-util | 自动发 DFU_DETACH |
| HID 命令 | `CMD_ENTER_DFU (0xFF)` |
| 镜像校验失败 | APP 头 CRC 错误时自动停留 DFU |

> 注意：EVKLite 上**复位/上电时按住 USER KEY 会先进 ROM ISP**（芯片 BOOT
> 功能），轮不到 bootloader 的按键检测；因此按键进 DFU 只在固件运行时有效。

---

## 4. 自调试（调试 EVKLite 板子本身）

DAP 目标脚（PA04–PA08）就是芯片自身的 JTAG 引脚：

- **APP 运行时**：这些脚被固件重配为 GPIO（DAP 输出），芯片自身 JTAG 不可用。
- **DFU / bootloader / ISP 模式**：固件完全不碰 PA04–PA08，J5 保持芯片
  JTAG 功能——此时用 J-Link（JTAG）即可调试/恢复固件。
  这也是"固件写挂了"的标准恢复路径（配合 USB ISP 双保险）。

> EVKLite **没有板载 FT2232 调试器**（见 UG §3.5 注），调试只走 J5 的
> J-Link/外部调试器；板上的 "USB to UART" 器件默认不贴片。

---

## 5. 注意事项

1. **J5.15（nSRST）是 EVKLite 自己的复位输入**（芯片 RESET_N），固件无法
   驱动它。用 20 针排线直连目标板的 JTAG 座时，不要让目标板的复位网络
   反灌 J5.15；SWD 目标建议直接杜邦线取 §1.1 的信号。
2. J5 的 PA04–PA08 与 DAP 固件共享：DAP 固件运行时不要同时接 J-Link
   连接，否则会争用引脚。
3. DAP 的 nRESET（PA08/J5.3）在 DFU 模式下呈现为芯片 nTRST 输入（高阻带上
   拉），外接目标复位线不受影响。
4. 无电平转换：DAP I/O 为 3.3V，目标板电平需匹配；无 VREF 检测，HID 的
   电压/LED2(VREF) 模式在此板上无意义（LED2 默认关闭）。
5. **CDC 回环必须短接 J3.8 ↔ J3.10**（UART_TXD ↔ UART_RXD）：只接逻辑分析仪
   不构成回环。固件侧全链路（复用/时钟/DMA/桥）已在 2026-09-27 实测通过，
   无回显时优先查这两个脚。

---

## 6. 验证清单

- [x] `build_xip_evklite.bat` / `build_dfu_evklite.bat` 编译通过，输出
      `[pack boot]` / `[pack app]`。
- [x] `flash_jlink_evklite_full.bat` 首刷后，USB0 口枚举
      `VID_0D28 PID_0204`（CMSIS-DAP 复合设备：DAP + CDC + HID + WebUSB + DFU）。
- [x] OpenOCD/pyOCD 等 SWD 主机可连外部目标（SWCLK=J5.9, SWDIO=J5.7）。
- [x] 长按 USER KEY 1s → 设备重枚举为 `PID_0207`（DFU+MSC 虚拟 U 盘）。
- [x] CDC 串口回环：**短接 J3.8 ↔ J3.10**，`script_test/evk_echo.py` 与
      `uart_loopback_common.py` 全速率通过（9600~10 Mbps，10M→974 KB/s）。

> 2026-09-27 实测记录（`script_test/evk_diag.py` 诊断固件）：
> UART3 内部回环自测 8/8、主机 27 字节经 CDC→UART TX 全部发出（TX DMA 完成
> 中断触发）、引脚直连自测确认 J3.8/J3.10 已短接；全速率回环 PASS。
