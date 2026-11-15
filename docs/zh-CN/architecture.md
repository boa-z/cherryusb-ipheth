[English](../architecture.md) | [简体中文](architecture.md)

# 架构与生命周期

`src/ipheth.c` 不分配内存，不调用 OS 或 USB 传输接口，不进行未对齐的整数读取，除 C 标准库外没有其他依赖。调用方提供完整的 USB RX 传输和同步帧消费回调。调用任何回调前，解析器先完整校验 NCM 表，包括终止项、算术边界、帧长及重叠情况。回调返回后不得保留 RX 数据视图。

Class Driver 解析已选定的配置，校验 Apple VID 和 ff/fd/01，再选择同时具有一个 bulk IN 和一个 bulk OUT 的最低 alternate。PID、USB 配置值、接口号和端点地址均不固定。当前 CherryUSB 外部 Class API 只有组合 VID/PID 匹配表，因此本驱动的 class 表精确匹配接口三元组，并在 `connect` 中拒绝非 Apple VID。

每个设备独立持有 URB、端点描述符、DMA 缓冲区、控制请求 Setup、信号量和 TX 互斥锁。EP0 Setup 不复用 `hport->setup`；USB 栈的控制传输锁负责与其他 class 串行访问 EP0。数据传输不使用共享 EP0 互斥锁。USBMUX 独立保留自己的接口、私有数据和 URB。

设备 worker 提交异步 RX，并通过信号量接收完成通知；回调只发布结果，不重新提交，也不访问 USB 栈。worker 经网络适配器复制帧。默认激活遵循 Linux IPHETH：首次成功 Bulk IN 后才轮询 Carrier；空传输或四字节 00 01 xx xx 控制通知建立 `rx_ready`，不交付 Ethernet 帧。外部已独立激活的宿主会话可通过可选的 `usbh_ipheth_host_ready(hport, true)` 通知，在 RX 成功提交后、首个完成前允许查询真实 Carrier。上线和 TX 仍须 Carrier 正向应答；在 RX 实测就绪前，TX 还要求 `host_ready` 和 `rx_armed`。撤销通知会立即阻止首个 RX 前的 TX 路径，由控制 worker 降低链路；正在执行的 Carrier 查询返回时也重新检查激活状态。通知不是驱动执行了认证。拔插重连清零。独立控制 worker 使用允许回绕的单调时钟每秒查询 Carrier，私有 EP0 互斥锁与 halt 恢复共享。已建立链路连续三次事务失败后下线；成功查询明确 down 时立即下线。正常 RX 不等待 TX 互斥锁，只有真实 TX STALL 恢复才获取。USB 错误按退避策略重试，TX 同步超时为 1 秒。激活通知生命周期要求见[移植说明](porting.md)。

拔出时依次执行：标记 stopping；等待 RX 和控制两个 worker 确认；取消未完成的 RX；报告链路下线；等待网络 TX 使用方退出；只清除本接口的私有指针；删除同步对象并回收槽位。每个 worker 发出确认是它对设备存储的最后一次访问。任一 worker 创建失败都会回滚网络适配和全部五个 OSAL 对象。如果 HCD 取消失败，就隔离设备槽位及其对象，避免复用可能仍被 DMA 或回调访问的内存。该策略有意占用此槽位，直到重启。

RT-Thread 适配器在设备拔出期间仍保留已注册的小型 `eth_device` 对象和互斥锁，因此排队中的 tcpip 数据包不会引用已经释放的 netif 内存。设备离线时拒绝 TX；重连复用设备，在 lwIP 线程中更新 MAC 并重新启动 IPv6 Link-Local DAD。RX pbuf 不引用 USB 存储。移植层必须使所有公开 send/stats 调用方与 detach 回调串行化。回调不得递归断开设备，也不得在 RX 回调中发送。

当前 RT-Thread 适配器要求 TCP/IP 线程在 USB 枚举前已经启动，这符合通常的 RT-Thread 组件初始化顺序。适配器不强制更改默认路由；socket 使用方应选择实际需要的网络接口或源地址。

RT-Thread 直接接收适配器通过同步 netifapi 调用通知链路 up/down，包括 detach。它不使用共享 Ethernet RX 邮箱，后者的非阻塞投递可能在实体以太网繁忙时失败，丢失唯一 carrier 变化通知。此核心串行化操作不持有 TX 互斥锁，保留 IPv6 DAD 与实际 carrier 就绪条件，不强制接口就绪。

正常 RX 重新提交不获取同步 TX 互斥锁。只有已报告的 TX STALL 才进入互斥保护的恢复路径，重新检查暂停和停止状态，并通过私有 EP0 存储清除端点暂停及复位切换位。待恢复标志通过 OSAL 临界区发布。真实工作线程回归用测试门闩阻塞 TX，要求在释放门闩前已产生新的 RX 提交，覆盖 NCM 和 Legacy 两种模式。这消除了一项 RX/TX 锁依赖；如果网络适配层需要在 ACK 发送等待期间维持接收进度，也必须避免在 RX 回调中同步执行协议及输出操作。
