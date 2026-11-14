[English](../validation.md) | [简体中文](validation.md)

# 验证记录与板端步骤

本文保留 2026-11-10 的历史硬件证据和调查阶段。各阶段的待验证说明只描述当时状态；下方后续结果取代了早先零流量观察。V2.1 文档版本没有驱动行为变更，尚未进行硬件运行。本次本地测试和文档审计记录见[文档审计](documentation-audit.md)。

## 当前硬件结果：v13（2026-11-10）

用户提供的 UART 日志建立了首个工作的 D213 数据通道证据。外部应用激活完成 Pair、StartSession 和服务启动，并报告 session 和 service TLS 均已启用。这些操作属于应用，没有加入本驱动，该服务也不是通用 IPHETH 的必要条件。

```text
i0: attached MAC de:b5:4f:73:dd:03 RX=79 TX=18 rxerr=1 txerr=0 usberr=0
USB RX: submitted=82 completions=81 bytes=33714 control=1 ready=yes carrier=up
TX failures: frame=0 submit=0 short=0 last=0 length=0 actual=0
IPv4: 172.20.10.4/28 gateway=172.20.10.1 DNS=172.20.10.1
IPv6 Link-Local: FE80::DCB5:4FFF:FE73:DD03 VALID
```

这证明了 Ethernet 接收、发送及 DHCP，且两个复合接口同时保持可用。后续应用运行公布该 IPv6 端点，并在对端关闭 TCP 连接前收到六个 RTSP 请求，包括 pair setup/verify 和 authentication。媒体流尚未启动。该应用行为在本组件范围外。81 次完成后端点 86 又有一个待完成 URB，不能据此认定接收路径卡住。

一次 RX 错误尚未解决：该计数涵盖超长完成、包解析拒绝和网络交付失败。USB/TX 错误为零不能解释它。改变帧格式前应先在有流量时比较计数器。专门的对端 IPv6/NDP 测试、持续负载、legacy 模式硬件覆盖及断开/重连压力仍待完成。下方零流量历史结果描述的是较早构建，不是当前结果。

继续使用保存的 v13 镜像；这些检查不需要重新烧录：

```text
ipheth_status
ping 172.20.10.1
ipheth_status
ifconfig
carplay_usb net
```

按用户提供的 ifconfig 输出，以 i0 为默认接口并使用单参数 ping。SDK shell 的双参数帮助与实现参数顺序不一致，因此步骤避开该形式。如果租约网关变化，应使用当前网关。记录真实 ping 输出；DHCP 成功本身不证明手机响应 ICMP。保持工作的激活会话打开。之后进行受控重插时，收集拔出前后状态，必要时在外部重新激活，并检查只复用一个 i0、流量恢复且没有过期回调或资源增长。这里不宣称已实现自动配对持久化或重连激活。

## v13 追加板端结果：网关 ICMP 成功

之后提供的 UART 运行收到来自 172.20.10.1 的全部四次网关 ping 回复，TTL 64，RTT 分别为 2、1、1、1 ms。后续状态为 RX=85、TX=16、rxerr=4、txerr=0、usberr=0；70 次 USB 接收完成共 52279 字节，接口仍为 ready/carrier-up，且本机 IPv6 地址有效。NCM 每次完成可以包含多个帧，因此 RX=85 而 USB 完成为 70 本身不矛盾。

早先快照为 rxerr=1，但本次 USB 地址为 4，早先为 2，其他计数也不同。不得把两者当成连续计数区间。四次错误需要解析器/长度/网络交付的独立诊断；它们不能证明 USB 传输失败，也不能排除 datagram 丢失。应在同一次连接中边 ping 边采集连续状态，以判断错误是否持续增加。

应用报告 paired=1，且有完整 StartSession/StartService 回复，但在较晚服务阶段超时。即使应用失败，可移植 Ethernet 接口仍可用，网关 ping 仍成功。这进一步说明服务就绪与 Ethernet 就绪需要独立状态和恢复策略。

## 自动化结果（2026-11-10）

- CMake/CTest Debug：协议、class 生命周期及实际上游 EP0 测试通过。
- CMake/CTest ASan + UBSan + 默认泄漏检测：基于升级 SDK 引入的官方 CherryUSB 提交 a0054795，三个测试全部通过。
- Host 源码/测试使用 `-Wall -Wextra -Werror`。
- 协议测试：描述符截断/变异、非 Apple VID、非固定接口 7/alternate 顺序、MAC/carrier 回复变体、legacy 边界、NCM 重叠/截断/终止项/链表拒绝、64 KiB 边界、回调失败、TX 边界/精确长度提交及 50,000 个随机畸形输入。
- Class 测试：allocation/MAC/NCM/net/thread 失败回滚、NCM 和 legacy 协商、失败/短 TX、20 次重连、RX/TX STALL 恢复、断开期间并发 TX、双设备/槽位耗尽、取消失败隔离及 detach 后的晚到回调。
- 实际上游 `usbh_control_transfer`：零数据成功、6/64 字节 MAC 回复、carrier 字节、不完整 Setup 计数、STALL 重试、超时、设备离线及无效参数。这专门保护了仅靠 mocked class 测试未覆盖的长度契约。
- 从 SDK 分支 poc/carplay-oss 完整链接并打包 D213 固件。ELF 检查确认保留的 class 段中同时存在 `ipheth_class_info` 和 `carplay_usbmux_class_info`，并包含 `ipheth_status`。
- rt-carplay Host 测试和平台边界检查通过。后者只检查 C/H 文件，避免 SDK 生成的对象文件产生误匹配。

这些测试不模拟 USB 电气行为、HCD cache 一致性、Apple 信任状态或真实 TCP/IPv6 互操作。

## D213 镜像

SDK 工作树：`/home/boa/Documents/luban-lite-jc-carplay-oss`。外部组件：同级检出目录 `../cherryusb-ipheth`。外部应用：同级检出目录 `../rt-carplay`。

```sh
scons --apply-def=d21x_demo128-nand_rt-thread_rtcarplay_defconfig
scons -j8
```

构建写入以下可被后续覆盖的输出路径：

```text
output/d21x_demo128-nand_rt-thread_rtcarplay/images/d21x_demo128_nand_carplay_page_2k_block_128k_v1.0.0.img
```

当前推荐且经过硬件测试的产物为 `/home/boa/Documents/carplay-test-images/2026-11-10-cherryusb-1.6.1-v13/d213-carplay-ipheth-cherryusb-1.6.1-v13.img`。大小 4,237,824 字节，SHA256 为 `72b542a84c1600799df01e696ebc017e46051a35a1cb2c5a019621799f3c989d`。相邻 manifest 固定 SDK `a6c3a34a4`、应用 `e168c31`、驱动 `25792b9`。SDK 上游 CherryUSB 仍为 a0054795，SDK 专用 HCD 集成独立保留。V13 参照 usbmuxd 将应用 USBMUX 读取限制为 16 KiB，增加原始错误诊断和取消前控制器快照。其成功不能隔离究竟是哪项变更或时序差异解决了先前失败。后续构建会覆盖 SDK 可变输出，因此应使用保存的产物。

### 历史 v7 产物与诊断

唯一 v7 诊断产物为 `/home/boa/Documents/carplay-test-images/2026-11-10-cherryusb-1.6.1-v7/d213-carplay-ipheth-cherryusb-1.6.1-v7.img`。它包含官方 CherryUSB 1.6.1 a0054795、应用 DMA 修复 `382b293`、IPHETH 就绪修正和 RX 诊断 `1283913`。V7 保留 SDK EHCI short-IN 修正 `88ef9b599`，并增加只读控制器诊断 `fa685b04c`。应用 `8cc06e0` 缩短了控制台网络提示。V7 完成整个固件构建，并有下方解释的用户控制器快照。该代码的普通及 ASan/UBSan Host 测试均为三个全部通过。此次复测不能使用前一个镜像，即 SHA256 以 5df9aa72 开头的产物。

固件启用 USB1 host 和 IPv6。当前镜像启动提示为 `rt-carplay: run carplay_start, then carplay_status ...`；早期 Phase 1 hardware-probe 提示对应另一应用/镜像。采集网络结果前先确认存在 `ipheth_status`。板端报告已确认新入口提示、正确配置选择、USBMUX 绑定和 IPHETH class 匹配。首次尝试随后在 Ethernet 初始化期间以 -2 失败，旧枚举器断开 USBMUX。已确认原因为旧控制长度契约；官方栈和未激活 class 的失败处理解决了它。v2 硬件尝试更早停在应用配置扫描：因提交未对齐的栈 Setup/描述符缓冲区，EHCI 触发断言。v3 应用经自有、cache-line 对齐并填充的 DMA 存储复制 control 和 USBMUX 缓冲区。官方核心和 EHCI 断言保持原样。Host ASan/UBSan 回归现在编译真实应用 port，覆盖未对齐输入、短回复、边界、分配失败、超时取消和晚到回调。V2，即 SHA256 以 02d34f32 开头的产物，已被取代。V3 后续成功枚举和绑定，板端结果见下文。当时实际网络流量仍待验证，且本地无板卡/串口访问。

## 板端步骤（115200，8N1）

先断开手机，再执行：

```text
carplay_usb start
```

将已解锁手机插入 USB1。必须先 start 再插入，因为应用在 start 时启用配置选择 hook。如果手机已经插入且只枚举 PTP（06/01/01），应在 start 后重插。本驱动自身从不切换 USB 配置。

采集全部枚举日志，然后执行：

```text
carplay_usb status
ipheth_status
ifconfig
carplay_usb net
```

提供日志的手机预期描述符为：配置索引 3/值 4，USBMUX 接口 1，Apple ff/fd/01 接口 2。驱动日志应说明选中的 alternate 和 bulk 端点，已观察为 alt 1、IN 86、OUT 05；`i0` 必须注册。这些预期观察不是其他设备的匹配规则。GET_MAC 应只报告数据字节，6 字节或填充至最多 64 字节的回复；ENABLE_NCM 成功应返回 0。STALL 可选择 legacy 模式。其他初始化失败应记录具体步骤并保留 USBMUX；解释网络状态前先收集该诊断。

配对/信任在驱动之外完成。将 no-carrier 或静默流量判断为帧格式失败前，先通过现有受信任宿主/应用解决它。不要在此添加配对代码；USBMUX/Lockdown 失败应独立记录。

五秒后再次执行 `ipheth_status`、`ifconfig`、`carplay_usb net`。检查 carrier、Link-Local 从 tentative 到 valid 的 DAD、RX/TX 增量和错误计数。已知对端 Link-Local 地址后，从对端向板端地址测试 ICMPv6，并带上对端 USB 接口 scope。Linux 对端对应 `ping -6 <board-link-local>%<peer-usb-interface>`；iPhone 需要支持 IPv6 的诊断应用/端点。不要猜测对端地址。采集 NDP/ICMPv6 和两次计数快照。如果板端镜像没有 IPv6 ping，可使用对端或正常带 scope 的 AF_INET6 socket 测试。

然后拔出并查询 `ipheth_status`/`ifconfig`：持久 i0 应仍注册，但已 detached、link down。重插后每个槽位应只有一个适配器；若 MAC 改变则更新；重新 DAD 并恢复流量。至少重复 100 次，包含活动流；确认无崩溃、过期回调、持续增长的 heap/thread 数量或对 USBMUX 的干扰。测试 TX/RX 期间拔线、拒绝信任、RX/TX STALL，有条件时测试第二部手机。

只有 IPHETH 网络通过后才继续应用会话；应用使用 socket 和已配置的公开 i0 接口。本阶段网络验证不需要启动应用 Receiver 会话。

## 仍需硬件证据

vendor request/reply 内容的 USB 抓包，UART 只确认 MAC/NCM 请求成功；线上 IPv6 DAD 和对端响应；Ethernet RX/TX 与带 scope 的 IPv6 连接；高速 cache 行为；负载下断开/重连及 USBMUX 共存；其他手机型号和 legacy 模式。Host mock/ELF 注册不能证明这些事项。

## V3 板端结果与 TX 诊断（2026-11-10）

用户提供的 v3 镜像 UART 日志确认：

- 配置索引 3/值 4 被选中；USBMUX 连接接口 1，在 IPHETH 绑定接口 2、alternate 1、IN86/OUT05 时保持连接。
- GET_MAC 返回 6 数据字节，ENABLE_NCM 返回 0，选择 NCM RX。
- RT-Thread i0 注册，carrier 为 UP，lwIP 报告 Link-Local 地址 VALID。仅本机状态不能证明对端可达，也不能证明 DAD 包成功发送。
- 快照：RX=0 TX=0 rxerr=0 txerr=10 usberr=0。当时没有成功 Ethernet 帧传输证据。原有 usb_errors 计数涵盖 receive/worker control 路径，不涵盖 TX 提交失败。

新增 `ipheth_status` TX 诊断区分 `frame`（协议校验）、`submit`（HCD 负返回值）和 `short`（成功但 OUT 长度不符）。`last` 是最近失败结果，`length` 是原始 Ethernet 长度，`actual` 是 HCD OUT 长度。之后成功传输不会清除最近失败详情。错误号含义取决于类别：协议错误与 CherryUSB 错误属于不同命名空间。Host 测试断言每一类别。修改手机设置或将错误归因于配对前，先间隔五秒采集两次状态。

配对/信任仍是外部职责，此诊断没有配对变更。DHCP-enabled 标志和 IPv4 0.0.0.0 不代表手机提供 DHCP 服务。IPv6 测试必须使用已知对端地址和接口 scope。

## 已发布验证

初次公开 main 推送（`1518ceb`）的 GitHub Actions 两个 Host 矩阵任务，普通及 ASan/UBSan，均通过：https://github.com/boa-z/cherryusb-ipheth/actions/runs/42955915884

## V4 板端结果与首次 RX 就绪修正

v4 UART 快照为 RX=0 TX=0 txerr=12，frame=0 submit=12 short=0 last=-14 length=70 actual=0。在固定官方 CherryUSB 中，-14 为 USB_ERR_TIMEOUT。适配器使用同步 TX，因此这是等待完成超时，不是描述符校验或短 OUT。没有 USB 抓包，无法据此证明设备 NAK、外部信任缺失或控制器调度/完成有问题。

复查 Linux ipheth 可见，confirmed_pairing 在首次成功 Bulk IN 完成后才置位，包括空传输/控制传输，此前不执行 carrier 检查。本驱动此前遗漏该门控，仅凭 carrier-on 回复便允许 lwIP 发送。V5 采用 Linux 行为，过滤空传输和四字节 00 01 xx xx 控制传输，公开 Bulk IN 提交/成功/字节/控制计数和 RX 就绪状态。就绪/carrier-up 前的 TX 本地拒绝并计为 deferred。这修正了过早上线，但不证明手机无接收数据的原因，也不宣称配对/USB 流量已修复。

测试覆盖 RX 前抑制 carrier 轮询、本地 TX 延后且不计 USB 错误、空/控制完成建立就绪、不误交付 Ethernet 或报告解析错误、就绪后上线及重连状态清除。此前的 TX、STALL、并发拔出和隔离用例继续启用。

下一次板端运行，应在连接后和五秒后分别采集 `ipheth_status`、`ifconfig`、`carplay_usb net`。没有 Bulk IN 完成时，预期 ready=no、carrier=down，且无 IPHETH Bulk OUT 提交。本机 Link-Local VALID 仍不足以证明有流量。若始终未就绪，应分别调查外部信任和控制器/USB 抓包；不要在 Ethernet 驱动加入配对协议。

## V5 板端观察与 SDK EHCI 复现（v6）

V5 UART：i0 attached，RX/TX/errors 均为零，submitted=1 completions=0，ready=no、carrier=down、LINK_DOWN。这确认门控工作，但不能证明是手机静默还是完成事件丢失。

实际官方 EHCI HCD 检查发现另一个可复现的控制器缺陷。64 KiB IN 请求拆为四个 16 KiB qTD。较早 qTD 遇短包后沿终止 alternate link 结束，后续 qTD 保持 Active。旧扫描器仍等待所有后续 qTD 完成，且只有最后 qTD 设置 IOC。使用实际原版 HCD 的软件描述符/MMIO 复现确认，首个四字节传输会一直 pending。

SDK 提交 `88ef9b599` 在 `cherryusb-luban/` 下增加独立 unified patch 及原版/补丁版 ASan/UBSan 回归。构建只修改生成的 HCD 副本，全部 933 个官方源文件与 a0054795 保持逐字节一致。补丁为每个 bulk IN qTD 启用 IOC，识别终止短 IN/错误，只统计已完成描述符，并刷新 overlay data toggle。Control IN 短数据仍等待 status 阶段。测试覆盖首个或中间零/短 IN、完整 IN/OUT、pending 链、halted qTD、cache hook、data toggle 和 control status。原版复现和补丁测试通过，D213 固件链接打包完成。该 HCD 修正不含 IPHETH 帧格式/接收大小规避或配对变更。该补丁已准备供上游审查，尚未创建 PR。

V6 仍需证明真实 RX 完成。采集相同状态快照和 USBMUX 状态。软件复现不代表硬件成功；如果完成仍为零，外部激活/信任及 USB 抓包/控制器状态仍是独立调查路径。

## V6 板端结果与 V7 只读控制器诊断

v6 UART 仍为 RX=TX=0、submitted=1 completions=0、ready=no/carrier=down，全部错误为零。HCD 短包缺陷可在软件复现，但修正未建立硬件 RX 成功证据。不要宣称它解释该设备未完成 RX，也不要仅据此推断拒绝信任。

可移植驱动不变。SDK `fa685b04c` 增加 FINSH `usb_ehci_status [bus]`，只读采集寄存器/frame/IRQ 和活动 QH/qTD 快照，每 QH 最多八个描述符并校验池地址。它不取消、重提、改 alternate、确认 IRQ 或写硬件。检查会 invalidate 描述符 cache line；采集期间 DMA 仍可能改变硬件状态。采集器通过原版/补丁版 HCD Host ASan/UBSan 测试，覆盖 MMIO/token 不变、坏指针、循环链和无效 bus。D213 固件构建及保留 FINSH 导出已验证。v7 没有启用新的配对或帧格式行为。

连接后严格按顺序采集：

```text
ipheth_status
usb_ehci_status 1
carplay_usb desc
usb_ehci_status 1
ipheth_status
```

描述符转储在两次控制器快照间提供已知控制流量；不要加入其可选 request 参数。比较 RUN/异步调度位、frame 进展、IRQ 计数及端点 86 的 overlay/qTD 剩余字节。Active qTD 不变不能区分静默/NAK 设备与所有控制器故障；握手层结论仍需 USB 抓包。无关的 Lockdown/配对问题继续位于本组件之外。

## V7 板端证据与协议审计

提供的 v7 快照显示 IRQ 36 -> 54、FRAME 推进、描述符读取正常，以及一个待完成的端点 86 RX，四个 Active qTD 均剩余完整 16384 请求字节。URB err=-6 表示进行中。ELF 符号确认 bus 1 QH0 位于 4035fd00，async head 位于 40362080；head_link=4035fd02 和 QH0 link=40362082 构成有效环。早先自链接解释有误。未见 RX 字节进展或已完成短包。这支持下一步检查外部激活，但不证明缺少信任，也不排除端点特有的 DMA/调度故障。

[协议审计](protocol-audit.md) 交叉核对 Linux 变更历史、FreeBSD、原始 ipheth-pair、usbmuxd preflight 和 libimobiledevice。一项代码修正遵循 Linux 3e65efcca87a：两种模式都保留原始 Ethernet TX 长度，不增加最小 60 字节填充。修正后普通和 ASan/UBSan CTest 均为三个全部通过；核心测试检查精确长度/容量、数据不变及后方 canary，class 测试检查两种模式的 42 字节 OUT 提交。完整 D213 固件构建也通过。该修正不影响历史 70 字节 TX 超时，不能解释就绪前零 RX，当时尚未板端测试。

继续用已有 v7 采集外部激活证据，使用应用命令 `carplay_usb mux`，仅在没有工作会话时执行 `carplay_usb pair`，再执行 `ipheth_status` 和 `usb_ehci_status 1`。记录实际协商/会话阶段及信任对话框行为。应用后续服务启动错误不同于信任/会话失败。USBMUX/Lockdown 超时调查应在驱动外进行。当时成功 Ethernet 流量和重连压力仍待验证。
