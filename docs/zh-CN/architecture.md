[English](../architecture.md) | [简体中文](architecture.md)

# 架构与生命周期

`src/ipheth.c` 不分配内存，不调用 OS 或 USB 传输接口，不进行未对齐的整数读取，除 C 标准库外没有其他依赖。调用方提供完整的 USB RX 传输和同步帧消费回调。调用任何回调前，解析器先完整校验 NCM 表，包括终止项、算术边界、帧长及重叠情况。回调返回后不得保留 RX 数据视图。

Class Driver 解析已选定的配置，校验 Apple VID 和 ff/fd/01，再选择同时具有一个 bulk IN 和一个 bulk OUT 的最低 alternate。PID、USB 配置值、接口号和端点地址均不固定。当前 CherryUSB 外部 Class API 只有组合 VID/PID 匹配表，因此本驱动的 class 表精确匹配接口三元组，并在 `connect` 中拒绝非 Apple VID。

每个设备独立持有 URB、端点描述符、DMA 缓冲区、控制请求 Setup、信号量和 TX 互斥锁。EP0 Setup 不复用 `hport->setup`；USB 栈的控制传输锁负责与其他 class 串行访问 EP0。数据传输不使用共享 EP0 互斥锁。USBMUX 独立保留自己的接口、私有数据和 URB。

设备 worker 提交异步 RX，并通过信号量接收完成通知；回调只发布结果，不重新提交，也不访问 USB 栈。worker 经网络适配器复制帧。与 Linux IPHETH 一样，首次成功 Bulk IN 完成后，才轮询 Carrier 并发布链路上线。成功的空传输和四字节 00 01 xx xx 控制通知会建立接收就绪状态，但不交付 Ethernet 帧。此后使用允许回绕的单调时钟每秒轮询一次 Carrier。RX 尚未就绪或 Carrier 未上线时，发送返回 ENODEV，增加 tx_deferred，且不提交 USB 传输。该状态只反映数据通道就绪程度，不实现配对或信任协议。重连会清除就绪状态。USB 错误按退避策略重试；RX STALL 会清除 halt 并重置 RX toggle。TX 使用独立互斥锁同步执行，超时为 1 秒；TX STALL 的恢复交由 worker 处理。

拔出时依次执行：标记 stopping；等待 worker 确认；取消未完成的 RX；报告链路下线；等待网络 TX 使用方退出；只清除本接口的私有指针；删除同步对象并回收槽位。worker 发出确认是它对设备存储的最后一次访问。如果 HCD 取消失败，就隔离设备槽位及其对象，避免复用可能仍被 DMA 或回调访问的内存。该策略有意占用此槽位，直到重启。

RT-Thread 适配器在设备拔出期间仍保留已注册的小型 `eth_device` 对象和互斥锁，因此排队中的 tcpip 数据包不会引用已经释放的 netif 内存。设备离线时拒绝 TX；重连复用设备，在 lwIP 线程中更新 MAC 并重新启动 IPv6 Link-Local DAD。RX pbuf 不引用 USB 存储。移植层必须使所有公开 send/stats 调用方与 detach 回调串行化。回调不得递归断开设备，也不得在 RX 回调中发送。

当前 RT-Thread 适配器要求 TCP/IP 线程在 USB 枚举前已经启动，这符合通常的 RT-Thread 组件初始化顺序。适配器不强制更改默认路由；socket 使用方应选择实际需要的网络接口或源地址。
