[English](../porting.md) | [简体中文](porting.md)

# 移植

编译 `src/ipheth.c` 和 `port/cherryusb/usbh_ipheth.c`，添加 `include/` 以及 CherryUSB 的 core/common/hub/config 头文件路径。使用 RT-Thread/lwIP 时，还需编译 `port/rtthread/ipheth_rtthread.c` 并定义 `IPHETH_EXTERNAL_NET_ADAPTER`。启用 `RT_USING_LWIP` 时，`integration/SConscript` 会选择该适配器；集成它的软件包负责提供所选 CherryUSB 的头文件路径。

其他网络栈需实现 `usbh_ipheth.h` 声明的 attach/detach/receive/link 回调及单调毫秒时钟。未提供适配器时，weak attach 回调会拒绝绑定。时钟按 32 位自然回绕；RT-Thread 实现从 tick 计数器换算毫秒。

应用必须在 class 绑定前初始化 host 并选择所需配置。本组件不会更改设备配置、请求应用专用模式、执行配对或持有其他接口。手机可能要求先配对并建立信任，初始 MAC 请求才会成功；此类绑定失败需在满足前置条件后重新枚举。

使用静态库时必须调用 `usbh_ipheth_link_anchor()`，或使用 whole-archive 链接。优先直接链接源文件或 OBJECT。链接器必须保留 `KEEP(*(.usbh_class_info))`，部分工具链使用 `usbh_class_info`。只声明一个空 anchor 而不调用，不足以让链接器提取对应的静态库成员。

## DMA 与 OSAL 契约

默认两个槽位各自保留 64 KiB RX 缓冲区，以及对齐的 TX、control 和 Setup 缓冲区。它们都占用完整的 `CONFIG_USB_ALIGN_SIZE` cache line，并位于 `USB_NOCACHE_RAM_SECTION`。HCD 必须对 OUT/setup 执行 clean、对 IN 数据执行 invalidate，或者由平台将该段映射为 uncached。不可将这些存储放入控制器无法访问的内存。平台链接、内存和 cache 策略不属于可移植协议核心；每个移植目标都应实测高速传输。

HCD 必须在传输之间保留 URB data toggle，并在 `usbh_kill_urb` 成功返回时同步停止回调和 DMA。取消失败会隔离槽位。有界同步 control/TX 提交仅可在 DMA 已静止后返回，超时和错误路径也必须如此。已审查 SDK EHCI 实现，但物理 cache 和取消行为仍需板端测试。HCD 超时实现有缺陷时，无法在此层修复。

OSAL 信号量必须在不同上下文间同步完成状态内存。临界区必须排除 worker/中断访问，SMP 上还需覆盖所有核心。同一 hport 的 class connect/disconnect 回调必须串行执行。按需配置 `CONFIG_USBHOST_IPHETH_MAX_DEVICES`，默认 2，RT 设备后缀最多支持 10 个；worker 默认优先级 12，栈大小 4096 字节。

64 KiB RX 使用多个 EHCI qTD；需为并发 RX、TX、EP0 及其他接口预留容量。SDK 支持串联的 16 KiB qTD。不要假定其他 CherryUSB HCD 也支持该传输大小，或所有控制器都具有 cache 一致性。

RT-Thread Ethernet 设备在拔出后仍以 i0/i1 保持注册。注册使用 RT-Thread 正常的 Ethernet 初始化流程；ARP、ICMPv6、Neighbor Discovery、DAD、DHCP 和 IP 路由由 lwIP 处理。IPv6 需启用 `RT_USING_LWIP_IPV6`。状态命令为 `ipheth_status`；`ifconfig` 显示地址和标志。RT 设备的两字符名称可能不同于 lwIP 名称加数字索引；使用方应查询真实接口，不要假定索引为 0。

## EHCI 多 qTD 短 IN

64 KiB NCM 请求要求 HCD 在较早的 qTD 以短包结束时完成 bulk IN URB，即使后续 qTD 仍为 Active。固定版本 a0054795 的 EHCI 实现在此路径存在已复现缺陷。D213 SDK 带有独立且经过测试的控制器补丁；该补丁不包含在此可移植驱动中。参见 validation.md 中的 v6 记录。不要缩小 NCM 缓冲区或伪造标准 CDC 描述符来掩盖控制器完成处理缺陷。其他 HCD 也必须满足同样的短传输契约。
