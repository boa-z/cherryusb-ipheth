[English](README.md) | [简体中文](README.zh-CN.md)

# cherryusb-ipheth

## 项目概览

适用于 CherryUSB 的外部 Apple iPhone USB Ethernet Host Class。协议核心为 C99，不依赖 USB 库、操作系统或网络栈。随附网络适配器面向 RT-Thread 和 lwIP 2.x。

Host 测试和 D213 交叉编译通过。用户提供的 v13 板端日志确认：外部宿主激活后，USBMUX/IPHETH 复合接口绑定、MAC 读取、Apple NCM RX 协商、双向 Ethernet 流量和 DHCP 成功，地址 172.20.10.4/28、网关 172.20.10.1。接口具有有效 IPv6 Link-Local 地址；后续应用日志显示经已公布有线 IPv6 端点进行 RTSP 请求。驱动遵循 Linux IPHETH，在首次成功 Bulk IN 后才允许上线。记录了一次 RX 错误，其原因、持续流量及拔出/重插稳定性仍待验证。参见[验证记录](docs/zh-CN/validation.md)和[协议审计](docs/zh-CN/protocol-audit.md)。配对/信任是外部前置条件；本项目不实现配对、认证、应用会话或设备级 USB 配置策略。

## 功能

- 根据 Apple VID 和 ff/fd/01 识别接口，不依赖 PID、配置值、接口号或 bulk 端点地址。
- 根据描述符选择 alternate setting，独立持有端点。
- vendor MAC、NCM enable 和 carrier 请求；只在 NCM enable 请求 STALL 时回退 legacy，传输错误导致绑定失败。
- Legacy RX 两字节前缀、Apple NCM16 RX，以及两种模式下的原始 Ethernet TX。
- MTU 1500、MAC 校验、carrier 轮询、错误计数、RX/TX STALL 恢复。
- 独立设备 DMA 存储、有界 TX、协作 worker 停止、取消失败隔离，以及用于安全重连的持久网络设备。
- RT-Thread Ethernet 设备 i0/i1，支持 lwIP IPv6 Link-Local/DAD。

Apple NCM RX **不要求**标准 CDC-NCM 描述符，也不会让 TX 变成 NCM 传输。本 class 不持有 USBMUX，也不选择其他 USB 配置。

## 驱动架构

C99 协议核心校验描述符、vendor 回复、Legacy RX 和 Apple NCM16 块，不分配内存、不调用 OS。CherryUSB class 持有 USB 资源和每设备 worker。独立网络适配器将收到的 Ethernet 帧复制进宿主网络栈。USB 配置选择及激活/配对层由应用负责。回调上下文和生命周期规则见[架构](docs/zh-CN/architecture.md)。

## 协议支持

两种 RX 模式都按调用方原始帧长发送原始 Ethernet TX。支持帧长 14..1514 字节、MTU 1500。NCM16 支持已观察到的 Apple NTH/NDP 布局、最多 22 个 datagram、64 KiB 块，不支持 NDP 链或 CRC。交付帧前完成全部校验。VLAN、jumbo frame、NTH32、标准 CDC-NCM class 绑定和 NCM TX 不在实现范围内。改变帧格式或激活假设前，先阅读[协议审计](docs/zh-CN/protocol-audit.md)。

## 兼容性

固定 Host 参考为官方 CherryUSB 1.6.1 提交 `a0054795c44037ace8a2b77b6e610adbff2257ea`。RT-Thread 集成面向 lwIP 2.x，D213 交叉编译使用 lwIP 2.1.2。设备匹配依据 Apple VID 和 ff/fd/01，不使用 PID 列表。硬件证据覆盖用户提供的 iPhone 05ac:12a8、配置值 4，不代表所有型号兼容。参见[兼容性](docs/zh-CN/compatibility.md)。

## 构建与测试

使用 C99 编译器和 CMake 3.16 或更新版本。完整 class 测试还需 C11 编译器、POSIX threads 和外部 CherryUSB 检出目录。Sanitizer 测试要求 GCC/Clang 支持 AddressSanitizer 和 UndefinedBehaviorSanitizer。文档检查只需 Python 3，不发起网络请求。

只有协议核心不需要外部头文件：

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

基于外部 CherryUSB 检出目录编译和运行真实 class：

```sh
cmake -S . -B build-san -DCMAKE_BUILD_TYPE=Debug \
  -DIPHETH_SANITIZE=ON -DCHERRYUSB_ROOT=/path/to/CherryUSB
cmake --build build-san
ASAN_OPTIONS=detect_leaks=1 ctest --test-dir build-san --output-on-failure
```

也可以使用 `make check sanitize` 和 `make check-class sanitize-class CHERRYUSB_ROOT=/path/to/CherryUSB`。仓库不引入 CherryUSB fork。

单独运行双语文档检查：

```sh
python3 scripts/check-docs.py
```

## CherryUSB 集成

编译 `src/ipheth.c` 和 `port/cherryusb/usbh_ipheth.c`，添加 `include/` 及所选 CherryUSB 的 core/common/hub/config 头文件路径，并提供 `include/usbh_ipheth.h` 中的回调和 `usbh_ipheth_now_ms()`。未提供适配器时，weak network attach 拒绝绑定。`integration/` 提供构建辅助；参见[移植](docs/zh-CN/porting.md)和[示例](examples/README.zh-CN.md)。

优先直接源文件/OBJECT 链接。使用静态库时调用 `usbh_ipheth_link_anchor()` 或采用 whole-archive，并在链接脚本保留 `KEEP(*(.usbh_class_info))`。HCD 必须支持 64 KiB 多 qTD RX、短 IN 完成、有界同步传输和成功取消的同步静止保证。D213 专用 EHCI short-IN 修正属于 SDK overlay，不属于此可移植驱动。

## RT-Thread / lwIP 集成

还需编译 `port/rtthread/ipheth_rtthread.c` 并定义 `IPHETH_EXTERNAL_NET_ADAPTER`；启用 `RT_USING_LWIP` 时由 `integration/SConscript` 选择。USB 枚举前启动 TCP/IP 线程。IPv6 需启用 `RT_USING_LWIP_IPV6`。适配器注册持久 i0/i1 设备，将帧复制到 pbuf，经 lwIP 线程更新 MAC，并在连接时重新启动 Link-Local DAD。它不选择默认路由。应用使用普通带 scope 的 IPv6 或 IPv4 socket。

## USB 与网络生命周期

宿主在 class 绑定前选择 USB 配置。驱动只持有发现的 Ethernet 接口，选择其 alternate，读取 MAC 并协商 RX 模式。worker 等到首次成功 Bulk IN 完成后才轮询 Carrier/上线；空传输/控制完成可以建立 RX 就绪而不交付 Ethernet 帧。就绪不等于配对或认证。

TX 使用独立互斥锁同步执行，保留原有 1000 ms 超时；同时只有一个未完成 RX URB。Detach 停止 worker、取消 RX、报告下线、等待网络 TX 使用方退出，再释放接口资源。HCD 取消失败会隔离槽位直至重启。持久 RT-Thread netif 拒绝离线 TX，并在重插时复用。调用方必须将公开 send/stats/probe 操作与 detach 串行化，回调返回后不可保留 RX 视图。参见[架构](docs/zh-CN/architecture.md)。

## 诊断

`ipheth_status` 报告连接、Carrier/RX 就绪、帧和 USB 计数、TX 失败类别及最近失败详情。`ifconfig` 显示实际地址与标志。可选低优先级 `ipheth_performance_report()` 采样计时窗口；FINSH `ipheth_probe carrier off` / `ipheth_probe carrier on` 控制诊断实验，不绕过初始就绪。计时定义和 RX 错误分类见[延迟探针](docs/zh-CN/latency-probe.md)。USBMUX/Lockdown 诊断仍由应用负责。

## 硬件验证

| 证据级别 | 结果及范围 |
| --- | --- |
| Host Tested | 协议、真实 class 生命周期和上游 EP0 契约测试；普通及 ASan/UBSan |
| Cross-Compiled | 完整 D213 RT-Thread/lwIP 固件链接与打包 |
| Hardware Validated | 历史用户 v13 UART：复合接口绑定、NCM RX/原始 TX、DHCP、网关 ICMP、有效本机 IPv6；后续观察到应用 RTSP |
| Pending Validation | 当前文档版本的硬件回归、持续负载、物理重插压力、对端 IPv6/NDP、legacy 模式硬件及其他手机 |

历史镜像身份、准确日志、步骤和未解决 RX 错误见[验证记录](docs/zh-CN/validation.md)。Host 测试和交叉编译不证明 DMA/cache 正确或当前板端验收通过。

## 已知限制

默认两个设备槽位各保留 64 KiB RX 缓冲区和对齐 control/TX 存储。取消失败有意占用槽位直至重启。其他 HCD 需要独立验证 cache、超时、短传输和取消。接口设备在拔出时保持注册。初始化失败需在解决前置条件后重新枚举。配对、信任、USBMUX、Lockdown、MFi、CarPlay 会话及设备级配置策略均为外部职责。记录的 v13 RX 错误仍未分类，后续诊断计数器本身不能确定原因。

## 文档

英文为主维护语言；每篇维护文档都有完整简体中文版本和顶部导航。正文段落和列表项保持一个物理行，代码/日志保留必要换行。本地检查器验证语言配对、导航、相对文件/标题链接、命名、排版及机械翻译漂移指标，不能证明翻译语义准确。`LICENSES/` 中第三方原始法律声明保持原样、不翻译；生成构建产物不属于维护文档。

| 文档 | 内容 |
| --- | --- |
| [架构](docs/zh-CN/architecture.md) | 所有权、回调与清理 |
| [兼容性](docs/zh-CN/compatibility.md) | 版本、协议范围与设备观察 |
| [延迟探针](docs/zh-CN/latency-probe.md) | 计时窗口、Carrier 实验与 RX 诊断 |
| [移植](docs/zh-CN/porting.md) | 构建、DMA、OSAL、网络和 HCD 契约 |
| [协议审计](docs/zh-CN/protocol-audit.md) | 机制证据与激活边界 |
| [来源](docs/zh-CN/sources.md) | 来源、许可与上游准备 |
| [验证](docs/zh-CN/validation.md) | 测试结果、历史镜像与板端步骤 |
| [示例](examples/README.zh-CN.md) | 应用集成要求 |
| [文档审计](docs/zh-CN/documentation-audit.md) | 双语清单、例外与本地验证 |

仓库目录：

| 路径 | 用途 |
| --- | --- |
| `include/ipheth.h`, `src/` | 描述符、请求与包协议核心 |
| `include/usbh_ipheth.h`, `port/cherryusb/` | Host class 与网络回调边界 |
| `port/rtthread/` | Ethernet 设备、lwIP pbuf、IPv6、状态命令 |
| `integration/` | SCons 和 CMake 辅助 |
| `tests/` | 畸形包和 mock 环境下真实 class 生命周期 |
| `examples/` | 最小应用集成说明 |
| `docs/` | 架构、移植、兼容性、验证和来源许可 |

## 许可

新代码采用 Apache-2.0，见 [LICENSE](LICENSE)。协议参考和保留的 BSD 声明见[来源](docs/zh-CN/sources.md)与 `LICENSES/`。第三方原始声明独立保留；本项目不重新许可 CherryUSB、RT-Thread 或复制的操作系统驱动。

## 致谢

感谢 CherryUSB 和 RT-Thread/lwIP 提供 host 与网络基础设施，感谢 Linux 和 FreeBSD IPHETH 贡献者记录行为和协议历史。[来源记录](docs/zh-CN/sources.md)列出审查版本与保留声明。也感谢提供 D213 UART 证据的贡献者；这些观察结论仍限于记录的产物和设备。
