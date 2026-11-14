[English](../compatibility.md) | [简体中文](compatibility.md)

# 兼容性与限制

Host 构建基于官方 CherryUSB 1.6.1 master 提交 `a0054795c44037ace8a2b77b6e610adbff2257ea` 完成验证。Luban-Lite 当前引入该上游源码树，SDK 集成代码位于其外。本外部 Class Driver 不要求修改 USB 栈核心。USB 配置选择仍由应用制定策略。

旧 SDK 1.5 控制 API 的返回值包含八字节 Setup；首次硬件测试暴露了这一契约差异，Ethernet 初始化因此失败，旧枚举器随后又断开 USBMUX。该 USB 栈已不再是受支持的运行目标。编译实际上游核心的回归测试覆盖了官方 EP0 仅返回数据长度的契约。现在，已识别的 Apple Ethernet 初始化失败会使该 class 保持未激活，并保留其他复合接口，同时输出具体失败步骤。修复前置条件后需重新插拔，以重试初始化。

D213 RT-Thread/lwIP 2.1.2 适配器已经完成整个 SDK 的交叉编译。用户提供的 v13 UART 日志验证了外部宿主激活后的绑定、双向 Ethernet 流量、DHCP 以及有效的本机 IPv6 Link-Local 地址。后续应用日志还记录了经已公布的有线 IPv6 端点进行的真实 RTSP 交互。这只覆盖一个已观察的设备及配置，不代表所有 iPhone 都兼容。一次 RX 错误的原因、持续负载以及拔插压力测试仍待确认。Host mock 不能验证设备上的 cache 行为。

已提供的板端观察结果，证据来自外部 UART 日志：

- D213 USB1 host，iPhone VID/PID 05ac:12a8。
- 配置值 4，描述符索引 3。
- USBMUX 接口 1；Apple Ethernet 接口 2，ff/fd/01。
- Alt 0 无端点；Alt 1/2 为 IN 86、OUT 05，高速 MPS 512。
- 报告的四个配置中均无标准 CDC-NCM 接口。

这些值是测试观察结果，不是硬编码匹配条件。

协议范围为不带 VLAN 标签的 Ethernet，MTU 1500，帧长 14..1514 字节。Legacy RX 去掉两字节前缀。vendor request 04 成功时选择 Apple NCM16 RX，STALL 时选择 legacy RX。NCM16 的 NTH 位于 0，NDP 位于 12，最多 22 个 DPE，固定保留头部空间为 108 字节，签名为 NCM0，不支持 NDP 链或 CRC。解析器校验 NTH/header/block/table 长度，包括 64 KiB block-length 回绕，以及终止项和互不重叠、范围受限的 datagram。其他布局的设备必须先采集描述符和传输数据，再扩展实现并测试；不得静默猜测线上格式。VLAN、jumbo frame、NTH32 和 NCM TX 不在初始实现范围内。

与当前 Linux 一致，Carrier 同时接受单字节 04 回复，以及第 1 字节为 04 的多字节回复。与 FreeBSD 一致，vendor request index 使用实际发现的接口号，不假定接口为 2。过短或无效的 MAC 回复会导致绑定失败。这些协议选择仍需真实设备兼容性测试，尤其要区分 CarPlay 配置与普通个人热点场景。
