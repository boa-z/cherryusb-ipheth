[English](../latency-probe.md) | [简体中文](latency-probe.md)

# 延迟探针与 Carrier 单变量实验

保留原有 RX URB、TX 超时和取消策略；Carrier 现由独立控制 worker 执行：一个 RX URB、现有 1000ms 超时的同步 TX、初始就绪/Carrier 检查、周期 Carrier GET，以及原有断开和取消失败隔离。class worker、ISR 和 TX 路径均不打印日志。耗时使用允许回绕的单调毫秒时钟。

当前回归要求 RX 在 120ms Carrier 事务期间继续消费和重挂；两个 worker 创建失败及全部五处 OSAL 分配失败都必须完整回滚。下文旧阻塞实验属于历史记录，实机 FPS 改善仍待验收。

RT-Thread 应用可以从低优先级 worker 调用 `ipheth_performance_report()`。它每五秒通过非阻塞适配器锁采样各已连接接口，在短临界区内读取并清零计时计数器，然后释放适配器锁并打印简短控制台日志。指标包括次数、平均值、直方图 p95 上界和窗口内精确最大值。分桶为 <=0、<=1、<=4、<=16、<=64、<=256、<=1000、>1000ms；最后一桶以实际最大值作为上界。这些值是实际经过时间，不是 CPU 时间。

- `carrier`：整个周期/初始 Carrier GET，包含等待 EP0 锁的时间。
- `wake`：RX 完成回调到 worker 消费该完成结果的时间。
- `rearm`：RX 回调到下一次成功提交开始的时间；不含等待设备新包的空闲时间，包含错误恢复延迟。
- `parse`：NCM 解析和所有帧交付回调的耗时，包含宿主 SDK 配置的直接 lwIP input/ACK 处理。
- `tx_lock`：等待 class TX 互斥锁的时间，不含适配器外层锁。
- `tx_urb`：原有同步 HCD 提交的耗时，包含等待完成的时间。

`usbh_ipheth_take_probe()` 读取并清空一个窗口；每个设备只能有一个报告方。原有 `usbh_ipheth_get_stats()` 累计计数器不会重置。探针中的常规传输错误计数是累计值，RT-Thread 报告方计算其增量。跨窗口的完成/操作在计时结束时计入；这些窗口不与其他应用的视频计数器同步。

启用 FINSH 时：

```text
ipheth_probe carrier off
ipheth_probe carrier on
```

`off` 只跳过**初次 Carrier 查询之后**的周期查询，不强制上线，也不允许在 RX 未就绪时发送。它暂时保留最近已知 Carrier 状态，仅用于实验；USB 拔出仍正常清理，重连恢复轮询。调用 setter 时，调用方必须提供与 send/get_stats 相同的生命周期保证。

在同一次连接上持续显示变化内容：on 30 秒、off 30 秒、再 on 30 秒，每次切换后丢弃第一条报告。将视频 RX 节奏、窗口内 wake/rearm 与 Carrier 时间和 TX 耗时比较。如果 off 消除了 Carrier 样本却未改善 rearm/FPS，就不能认定周期 Carrier 是主要瓶颈。本项目不宣称已取得硬件 FPS 改善。

当前真实工作线程 Host 回归在 120 ms Carrier 操作尚未完成时验证独立 RX 消费及重新提交。另一个测试用门闩阻塞同步 TX，要求在释放 TX 前完成 RX 重新提交，覆盖 NCM 和 Legacy 两种模式；旧的无条件 TX 锁路径未通过该回归。同时保留 Carrier 开关、慢 TX 耗时、窗口复位及生命周期和错误场景测试。这些结果证明软件执行路径独立，不能换算为实机 FPS 提升。

# RX 错误分类（v26）

`rx_errors` 统计完成块处理失败次数，不代表丢失的独立 Ethernet 帧数。诊断接收 API 保留原有解析检查、返回码和回调顺序，分别记录 size/signature/header/block/table/DPE range/frame size/overlap/termination 失败和回调失败。回调失败仍会像原实现一样停止后续 datagram 交付；`callback_remaining` 统计当前及后续已通过验证但未成功交付的 datagram。

五秒报告输出非零原因计数，以及最近失败的 NTH16/NDP16 头字段（20 字节）、DPE 索引/偏移/长度、已验证/已交付数量。不保留 DMA 视图，不记录 Ethernet 载荷。RT-Thread 适配器单独统计 pbuf 分配、复制和 netif input 失败，保留最近 input 错误码。不增加逐帧 UART 输出。适配器和 USB 快照窗口边界略有不同，应跨多个窗口比较，不应要求单窗口严格相等。

已审查的 Linux NCM 回调没有强制执行本解析器的全部额外约束。出现 `block` 或 `table` 失败时，应检查抓包，不能据此删除边界检查。HCD/DMA 错误引起的头部损坏也是可能原因。v26 不改变传输、解析器接受范围、超时、DATA PID 或取消行为。

## IPv6 启动头部计数

`ipheth_status` 同时输出从 attach 起累计的 IPv6 RX/TX 头部观测：邻居请求（`NS`）、邻居通告（`NA`）、TCP `SYN`、`SYNACK`、`RST` 及最近源端口与目标端口。RX 在 lwIP input 之前统计；TX 统计 USB 发送成功，不代表对端收到。这些计数不验证校验和。IPv6 扩展头只计入 IPv6 总数，不猜测分片 TCP。计数不分配内存、不保留载荷，只在请求状态时打印，不实现应用策略，不改变报文接受或转发行为。


对 RT-Thread/lwIP，可通过排队执行协议输入，将同步 ACK 发送移出 RX 工作线程。适配层首次注册 netif 时输出 input=direct 或 input=queued，此策略由平台 lwIP 配置选择，可移植 Class 不设置 lwIP 策略。有界邮箱仍可能拒绝数据包，适配层会释放并计入 rx_input_fail。排查卡顿时，应对比该计数、rx_parse/rx_rearm 与 TX 耗时。
