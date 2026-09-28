# 实验记录：放大 DAP 响应尺寸（方案 A）— 未完成，卡在 USB 状态

记录：原实验分支 feat/dap-resp-size（已删除；本文件保留在 main 上）
日期：2026-09-28
状态：**代码已回退**（工作区 = main），结论与待办记在这里

## 动机

主机驱动那条路（`make sram-test` / `dump_image`）60 MHz 档只有 2443 KB/s，而同档
链路天花板是 3281 KB/s —— 差的 26% 全花在 **USB 往返次数** 上：

```
DAP_Info(0xFE) 报 max packet size = DAP_PACKET_SIZE = 512
⇒ 每次 DAP_TransferBlock 最多带 ~508 B 回来
⇒ 2443 KB/s ÷ 512 B ≈ 4770 趟/秒（每趟 ~210 µs，典型 HS 往返延迟）
```

思路：CMSIS-DAP 的 "packet size" 语义是**单条命令响应的最大字节数**（可跨多个 USB 包），
与端点 mps（HS 硬上限 512）无关。把它调大 ⇒ 主机自动用更大的块 ⇒ 往返次数降下来。

## 改动（三处，共 6 行）

1. `src/dap/DAP.h`：新增 `#define DAP_RESP_SIZE 2048U`（附语义说明与取值理由）
2. `src/dap/DAP.c`：`DAP_Info(DAP_ID_PACKET_SIZE)` 改报 `DAP_RESP_SIZE`
3. `src/usb/usb_composite.c`：`USB_Response[DAP_PACKET_COUNT][DAP_RESP_SIZE]`
   （`USB_Request` 保持 `DAP_PACKET_SIZE`，主机发来的命令都很短；
     EP 描述符的 `wMaxPacketSize` 也保持 512）

## 实测结果

| 取值 | 构建 | 现象 |
| --- | --- | --- |
| 4096 | ✗ 链接失败 | `.stack will not fit in region DLM`，DLM 溢出 **864 字节** ⇒ HPM5301 的 DLM 放不下 4×4096 |
| 2048 | ✓ 构建通过、能烧、HID/CDC 正常 | **OpenOCD 全挂**：`Error: error submitting USB read: Entity not found`（反复重试、设备复位都无效） |
| 回退到基线 | ✓ | **OpenOCD 依然报同一个错** ⇒ ⚠️ **不能归因于本次改动**，是主机侧/USB 句柄状态卡住了 |

关键旁证（说明**设备本身没坏**）：

- HID 通道正常（`evk_ident.py` 能读到固件编译时间）
- CDC 正常（COM52 在，串口可用）
- **DAP 引擎也正常**：走 HID 透传（`CMD_RTT` action 4/6）执行 CMSIS-DAP 命令有响应
  （`rtt_rawdap.py` 能跑完整流程，SWD 那几步返回 0x07 = 目标侧没 ACK，属目标未初始化）
- 唯独**主机经 bulk 端点读 DAP 响应**这条路失败

## 待办（探针 USB 重插之后继续）

1. **先重插探针 USB**，确认 `openocd ... -c "init"` 恢复正常、`make sram-test` 回到基线
2. 再做真正的 A/B：基线（512）vs `DAP_RESP_SIZE=2048`，看 60 MHz 档读是否从
   2443 → 接近 3281 KB/s
3. 若仍复现 `Entity not found`，怀疑这两条（按可能性排序）：
   - **HPM5301 的 USB 端口驱动可能不支持「一次 `usbd_ep_start_write` 超过 mps」**
     （需要多包状态机）；查 `usb_dc_hpm.c` 里 EP 写长度 > mps 的处理，必要时改成分包发送
   - CMSIS-DAP**v2 over bulk** 的约定可能要求 `packet_size == 端点 wMaxPacketSize`，
     主机（OpenOCD 的 `cmsis_dap_usb_bulk.c`）会据此设置读缓冲；若是这样，方案 A
     在这条主机路径上行不通，得走"自写主机工具 + 多包/异步 URB"（方案 B）
4. 另一个可选方向：**固件只对 v1/HID 路径放大响应**，或干脆把批量读内存也做进
   CDC 流（方案 C，同 RTT 桥，能到链路 89%）

---

## 最终 A/B 结果（2026-09-28 12:3x，主机侧 USB 恢复后）

主机恢复后（换了 USB 口，探针 CDC 变 COM5）重测，同口径对比：

| SWD 档 | 基线 RESP=512 读 / 写 | 实验 RESP=2048 读 / 写 |
| --- | --- | --- |
| 1 MHz | 85.7 / 85.5 | **FAILED（数据校验错）** |
| 2 MHz | 176.0 / 176.6 | **FAILED** |
| 10 MHz | 760.7 / 799.3 | （未继续） |
| 20 MHz | 1296.5 / 1347.3 | （未继续） |
| 36 MHz | 1905.5 / 2070.3 | （未继续） |
| 45 MHz | 2146.3 / 2333.9 | （未继续） |
| **60 MHz** | **2428.2（复测 2523.2） / 2906.8（复测 2868.7）** | — |

**结论：方案 A（按下述改法）不可行** —— 把 `DAP_Info(0xFE)` 报成 2048、响应缓冲放大到
2048 之后，`load_image`/`dump_image` 的数据**校验直接失败**（1/2 MHz 就错，与 SWD 时序无关，
是结构性错误）。最可能的原因：本工程的 USB 栈（CherryUSB + HPM5300 端口驱动）**不支持
一次 `usbd_ep_start_write` 超过端点 mps**，超过 512 字节的响应无法正确分包发出。

⇒ 要救活方案 A，必须先把「单次写超过 mps」这条路径做对：
  1. 查 `usb_dc_hpm.c` / CherryUSB 的 EP 写实现，确认是否支持多包（多数实现需要调用方
     自己按 mps 分包续传，或用 `usbd_ep_start_write` 的链式长度）
  2. 或者改成分包发送 + 完成回调续传（像 CDC IN 那条链的做法）
  3. 若驱动确实只支持 ≤ mps，则方案 A 在 bulk 路径上没有意义，只能走方案 B（自写主机工具
     + `DAP_ExecuteCommands` 批处理 / 异步 URB）或方案 C（把批量读也下沉进 CDC 流）

基线固件已重新烧回（CRC 0xDF382B3F，60 MHz 读 2523 / 写 2869 KB/s 复核正常），板子可用。
