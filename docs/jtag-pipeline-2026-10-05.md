# JTAG 小块读流水优化（2026-10-05，待上板）

基线为 GitHub `main` 的 `d636f3f`，分支 `perf/jtag-read-pipeline-20261005`。
网页工作副本同步到 `5739fd8`；此次没有修改网页，也没有改 GPIO 汇编时序、
默认 8 个 idle TCK、USB 端点、采样批次开关或单字检查周期。

## 修改

1. DMI 响应 BUSY 改为规范值 3，FAILED 为 2，保留值 1 不当成 BUSY。
   BUSY/FAILED 通过真实 TAP 路径写 `dtmcs.dmireset`，恢复 DMI IR、排空旧响应、
   作废 SBA/hold 缓存。BUSY 才增加 idle TCK（上限 255）。不使用 `dmactive=0`
   作为这个恢复动作，不自动重放结果不确定的写。
2. SBCS 读取/清错失败返回 `UINT32_MAX` 表示未知，而不是返回 0 伪装无错误。
   打开、重配、hold 和块收尾均按失败处理。连续写核对每个应答及最后一笔应答。
3. 自增读丢失应答后停止当前流水，由原有 `read()` 重新配置并从原地址重读整块；
   不在原流水上重发 SBDATA0 后继续拼数据。这适用于现有 RAM 采集/RTT 用途，
   不承诺具有读副作用的 MMIO 可安全自动重试。
4. 块读最后一次收数据时投出 READ SBCS，下一次 NOP 收回 SBCS。保留每块状态
   检查和 sticky 错误清除，不再额外排空后独立读取 SBCS。
5. 对齐头、短尾和非对齐主机指针共用有界拷贝；完整对齐字保留直接写入快路径。
   修复原对齐地址但长度非 4 倍数时越过输出缓冲末尾的问题，拒绝空指针和地址回绕。

## 扫描预算，不是实测吞吐

配置缓存命中、无重试、无 sticky 错误时：写地址/确认 2 次，点火 1 次，
N 次收数据（最后一次投 SBCS），NOP 收 SBCS 1 次，共 N+4 次。
冷配置另需 2 次。原实现稳态 N+6 次。

| 读大小 | 原扫描数 | 新扫描数 | 仅按扫描成本计算的吞吐上限提升 |
| --- | ---: | ---: | ---: |
| 4 B / 1 字 | 7 | 5 | 40% |
| 32 B / 8 字 | 14 | 12 | 16.7% |
| 1024 B / 256 字 | 262 | 260 | 0.77% |

4 B 这一行只指块读入口，不是已优化的 hold 单字采样路径。
实际收益仍受 C 拷贝、目标 SBA、错误重试、采样调度和 USB/页面处理影响。
BUSY 恢复提高 idle 后，后续操作可能更慢；这是数据正确性保护，不是提高时钟。

## 离线验证

```sh
make test-host
# Linux 主机（原 Makefile 默认使用 Windows cmd.exe）：
make SHELL=/bin/bash .SHELLFLAGS=-c PYTHON=python3 test-host
ASAN_OPTIONS=detect_leaks=0 JTAG_HOST_SANITIZE=1 python3 script_test/riscv_jtag_host_test.py
```

JTAG 主机测试编译完整生产 C 文件，只替换 SDK 引用和 MMIO 计时器，模拟汇编
扫描边界的单深 posted DMI、SBA 自增及真实 TAP 的 IR/DR 状态转换。
覆盖 1072 组目标/主机对齐与长度边界、256 组 N+4 扫描计数、32/64/96 拍检查边界、
sticky BUSY/FAILED/保留值、持续 BUSY、失败的最后一笔写、SBCS 读取/清错失败、
整块重读及缓冲区哨兵。AddressSanitizer/UndefinedBehaviorSanitizer 通过。
LeakSanitizer 在当前受跟踪执行环境无法运行；禁用的是泄漏检查，不是越界检查。
本引擎没有动态分配，测试使用固定数组。

Scope、RTT STOP、SPI DRAIN、TARGET guard 和 8 个 HSS 测量脚本测试通过。
当前没有 HPM SDK/交叉工具链或在线目标，未做完整固件编译、汇编反汇编和上板测速。
这些主机模型不能证明目标 DTM、GPIO 建立保持时间或真实 USB 下的行为正确。

## 上板门禁

在同一 HPM6800EVK、同一接线和电源下编译基线/新分支，保持相同时序旋钮：

1. 先运行 `hpm6800_selfcheck.py`，已知图案读回必须逐字一致。
2. 使用 `rbench` 比较 4/32/1024 B，多轮旧→新→旧；每轮重开恢复相同 idle，
   同时记录成功迭代数、实际 delay、SBA 错误与重试统计，不能只比字节率。
3. `riscv_pipe_integrity_test.py` 验证单变量每个样本的契约，检查 32 拍边界；
   J-Scope 单变量和多变量分别测速，验证开始/停止/重连。
4. `hpm6800_rtt_loss.py` 检查 RTT 丢失、重复、坏记录和 probe-host 差值；
   比较大块搬运应接近原来，不用 16.7% 小块模型解释整个 RTT 通路。
5. 用可控故障验证 DTM BUSY 清除后回到 DMI、SBA 地址从头重挂、写错误不自动重放。
   避免用未映射总线地址触发不可恢复的目标总线挂起。

F103CB 的 SWD 回归不能替代 RISC-V/JTAG 门禁。此次没有启用 JTAG 短批次或压缩
汇编相位；这两项仍留作独立、可测量的后续优化。

协议依据：[RISC-V Debug 1.0 JTAG DTM](https://docs.riscv.org/reference/debug/v1.0/dtm.html)，
特别是 DMI op、sticky 状态与 `dmireset` 的语义。
