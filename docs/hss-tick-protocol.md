# HSS 亚微秒周期协议（尚未上板测速）

旧 HID 0x32 action 7 保持不变：u32 周期单位 µs，包 version=1，时间戳单位 µs。
新增 action 10 CONFIG_TICKS，字段布局与 action 7 相同，周期单位改为 24 MHz tick。
最小周期 48 ticks（2 µs），最大 24000000 ticks（1 s）。

| 周期 | ticks | 名义速率 |
| --- | --- | --- |
| 3 µs | 72 | 333.33 kHz |
| 2.75 µs | 66 | 363.64 kHz |
| 2.5 µs | 60 | 400 kHz |
| 2.25 µs | 54 | 444.44 kHz |
| 2 µs | 48 | 500 kHz |

action 10 会发 version=2 的 DEF/DATA/STAT：
- 包头 u32 时间戳是原始 tick，约 179 s 回绕；主机先按 u32 去回绕，再除以 24 得 µs。
- DEF payload[4..7] 和 STAT payload[16..19] 的 u32 周期都是 ticks。
- 帧载荷、序号、类型、长度、flags 与 v1 相同。
- 新网页同时解析 v1 和 v2；旧网页不能使用 v2。

STATUS w0 bit2=1 表示支持 CONFIG_TICKS。STATUS w11 bit17=1 表示当前周期是 tick 单位，
w11 低 16 位为周期原始单位；与旧状态一样，长周期在这个紧凑字段里会截断，完整周期以 DEF 为准。
网页在发送 action 10 前检查支持位，旧固件应明确要求升级，不能把未知动作当成成功配置。

离线验证：`python script_test/scope_host_test.py` 编译实际 sampler C，替换 SDK 外设和 timer MMIO；
覆盖旧/新单位、posted read 值连续性、满包 flush、周期限幅、非法变量宽度。
完整 HPM 固件编译需原有 SDK/工具链，吞吐、采样抖动、USB 并发需上板验证。
