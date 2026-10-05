# ADC 独立 Bulk IN

ADC 数据使用新增 vendor-specific 接口的 Bulk IN 0x8C，HS MPS 512。
现有 RTT/JScope/SPI 的接口编号和端点不变。自定义 HID 仍为 64 bytes，
报告 ID 不变；HS bInterval=6，即 4 ms（FS 配置编译时取 4）。

控制命令仍为 HID 0x38，标准 action 请求长度 2：

| Action | 返回数据 | 用途 |
| --- | --- | --- |
| 9 | ADB1、EP:u8、version:u8、MPS:u16 LE | 查询数据端点 |
| 10 | token:u32 LE | OPEN；队列及周期总线必须空闲 |
| 11 | 无 | END；先停止周期任务，再结束流 |
| 12 | 无 | CLOSE；END USB 传输完成前返回 BUSY |

OPEN 后先挂起 WebUSB 读取，再用现有 0x37 上传和运行 ADC 周期程序。
流打开时禁止 HID READ/ACK 以及非 ADC 周期程序，避免队列双消费者。
停止时使用 0x37 STOP，确认清理完成，再 END、读取 END 包、CLOSE。
有限任务可自动发送 END；未启动就取消也可用 END 结束挂起读取。

每包头 16 bytes：ADC1、version:u8=1、type:u8（DATA=1/END=2）、
count:u8、stride:u8=26、token:u32 LE、fault:u32 LE。
DATA 附 1–19 条记录；每条为现有周期结果的 24 字节元信息与 2 字节 ADC 数据。
END 无记录。最大实际传输 510 bytes，以 USB 短包终止，不需要 ZLP。

沿用现有 32 条周期结果队列，单个 512-byte DMA 缓冲，完成回调后才确认出队。
队列满时停止并报告故障，不静默覆盖。USB reset/disconnect 作废任务代数和 DMA 状态。
增加 WinUSB MS OS 2.0 接口描述符；SPI 开启时描述符总长 810 bytes，EP0 缓冲改为 1024。

采样调度不变：GPTMR1 通道 1 唤醒主循环采集，最短周期 1 ms，最高请求 1000 Sa/s。
不新增常驻采样中断，不修改 RTT/JScope 搬运路径；周期引擎继续在其活跃时让路。
4 ms HID 可能增加控制请求频率，实际性能影响须板测，不能仅凭主机模型保证。

验证：`make test-host`（含 ADC producer）与 `make test-usb-descriptors`。
描述符测试需要 HPM_SDK_BASE 或相邻 hpm-sdk-reference。
发布前需编译目标固件、确认 Windows/Chrome 枚举，检查有限/连续/停止/拔插，
测试 1000 Sa/s 持续采集和 RTT/JScope 单独运行吞吐；当前主机测试不能替代这些板测。
