[English](../protocol-audit.md) | [简体中文](protocol-audit.md)

# 协议与激活审计 — 2026-11-10

本审计检查源码、变更历史和宿主激活流程，不从单个设备推测未公开命令。驱动保持独立，不依赖配对、应用会话或 SDK 私有 API。

## 实际审查的参考资料

- Linux `drivers/net/usb/ipheth.c`：sources.md 记录的快照，以及从上游 GitHub API 获取的该文件提交历史。
- FreeBSD `sys/dev/usb/net/if_ipheth.c`：sources.md 记录的快照。
- [原始 ipheth + ipheth-pair](https://github.com/dgiagio/ipheth/tree/c8df202085804a067f403324e234a992f428afa9)，版本 `c8df202085804a067f403324e234a992f428afa9`。
- [usbmuxd preflight.c](https://github.com/libimobiledevice/usbmuxd/blob/3ded00c9985a5108cfc7591a309f9a23d57a8cba/src/preflight.c)，版本 `3ded00c9985a5108cfc7591a309f9a23d57a8cba`。
- [libimobiledevice](https://github.com/libimobiledevice/libimobiledevice/tree/fa0f79190142bc309307967c058f89c1b36eb6b8)，版本 `fa0f79190142bc309307967c058f89c1b36eb6b8`；配对/会话工具和公共 API 仅作为外部集成参考审查，未引入仓库。

本组件未复制任何配对实现或 GPL helper 源码。社区报告仅补充这些一手来源，不能视为线上协议规范或这部手机状态的证明。

## 发现与实现决策

| 机制 | 证据 | 决策及剩余限制 |
| --- | --- | --- |
| carrier/TX 前首次成功 Bulk IN | Linux [bb1b40c7cb86](https://github.com/torvalds/linux/commit/bb1b40c7cb863f0800a6410c7dcb86cf3f28d3b1) 明确说明与信任有关的 TX 超时，并指出 iOS 在建立信任后会发送一个包 | 保留 v5 就绪门控。首次成功 USB 完成，包括空传输/控制传输，建立观察到的 RX 状态；状态名称不证明驱动执行了认证。 |
| 宿主激活与 USB Ethernet 分离 | 原始 ipheth-pair 使用 libimobiledevice；usbmuxd preflight 检查已存宿主记录、打开 StartSession，必要时配对 | MAC/NCM 成功和 USBMUX class 绑定不代表建立信任或有效会话。本仓库不加入 Lockdown 协议。信任 Linux PC 不等于信任 D213 宿主身份。 |
| 精确 TX 传输长度 | Linux [3e65efcca87a](https://github.com/torvalds/linux/commit/3e65efcca87a9bb5f3b864e0a43d167bc0a8688c) 记录 macOS 在 legacy 和 NCM 模式都不增加 URB 尾部填充 | 删除本组件增加的最小 60 字节填充，保留调用方帧长和字节。FreeBSD 仍填充至固定 TX 缓冲区大小，本实现不采用该相冲突行为。该修正不能解释就绪前零 RX。 |
| 仅 NCM RX，TX 仍为 Ethernet | Linux [a2d274c62e44](https://github.com/torvalds/linux/commit/a2d274c62e44b1995c170595db3865c6fe701226) 和 [be154b598fa5](https://github.com/torvalds/linux/commit/be154b598fa5) | 保留 vendor request 04、65536 字节 RX、NTH16/NDP16 解析及原始 TX。不查找标准 CDC-NCM 配置，因为此 Apple 接口为 vendor-specific。模式可能跨连接保持。 |
| Carrier 回复格式 | Linux [67927a1b255d](https://github.com/torvalds/linux/commit/67927a1b255d883881be9467508e0af9a5e0be9d) 记录 1/3/4 字节回复，包括 `04 03 03 00` = off、`06 04 03 04` = on | 现有解析器在存在第二字节时使用它，与 Linux 一致。只看第一字节不足以判断。iOS USB 模式不是 USB 配置值；配置值 4 不证明设备模式为 4。 |
| alternate 选择与请求顺序 | Linux probe 获取 MAC/启用 NCM，随后 open 选择 alternate 1；FreeBSD 选择 alternate 1、启用 NCM，再获取 MAC 并启动传输 | 两种顺序都有实现依据。本驱动按描述符先选 alternate 再启用 NCM，有 FreeBSD 先例，也已在板端控制日志中成功。无抓包时，不得凭空增加 alternate-2 重试或宣称改换顺序能修复问题。 |
| Legacy 回退 | Linux 和 FreeBSD 在 NCM enable 失败时回退 | 本组件有意只在 STALL 时回退。一般传输错误不能可靠证明已进入 legacy 模式，尤其 NCM 状态可能持续存在。其他失败会让 Ethernet 保持未激活，同时保留复合接口。 |
| 连续接收与通知 | Linux [74efed51e0a4](https://github.com/torvalds/linux/commit/74efed51e0a4)、[94d7eeb6c0ef](https://github.com/torvalds/linux/commit/94d7eeb6c0ef) 和当前回调 | 解析失败后继续挂接 RX；零字节/四字节 `00 01` 通知不作为 Ethernet；保留完成/就绪计数器。 |
| Legacy 大小 | Linux [f33d9e2b48a3](https://github.com/torvalds/linux/commit/f33d9e2b48a34e1558b67a473a1fc1d6e793f93c) 及后续 NCM 重构区分 TX 帧限制和两字节 RX 前缀 | 当前限制保持 Ethernet 1514、legacy RX 1516、NCM RX 65536。不要将旧大小规避措施应用到 NCM。 |
| TX 调度 | Linux [94d250fae48e](https://github.com/torvalds/linux/commit/94d250fae48e6f873d8362308f5c4d02cd1b1fd2) 记录异步提交前停止队列 | 本实现通过同步 TX 和独立互斥锁串行提交，没有复制 Linux netdev 队列 API。后续异步移植必须避免其中记录的完成/队列竞态。 |

## 实际 V7 控制器快照的解释

早先现场说明曾误将两个地址识别为自链接，ELF 符号纠正了这一解释：

- QH 池基址为 `4035f800`；每条总线十个 128 字节条目，因此 bus 1 QH0 为 `4035fd00`。
- Async head 基址为 `40362000`；每个条目 128 字节，因此 bus 1 的 head 为 `40362080`。
- `head_link=4035fd02` 因此指向 RX QH0，而 QH0 的 `link=40362082` 指回 async head。这是有效的双节点环。
- 控制器遍历此环时 ASYNCLISTADDR 寄存器可能变化；仅在这些地址间变化不代表损坏。

| 观察结果 | 可支持的结论 |
| --- | --- |
| IRQ 36 -> 54；FRAME 0fc9 -> 1b68；描述符读取成功 | 控制器时钟和 IRQ 处理在推进；RX 待完成时已知 EP0 流量正常。证据不支持整个控制器冻结。 |
| CMD 00010031 和 STS e008/c008 | Run 与异步调度已启用；快照未显示控制器 halted 或 Host System Error。 |
| RX URB err=-6 | 这是 USB_ERR_BUSY，表示正常进行中，不是已完成的 USB 错误。 |
| 四个 qTD token 保持 40008d80 | 每个都是 Active、IN、IOC，剩余 16384 字节；未显示字节进展或终止 halt/error。这不是此前复现的首 qTD 已完成短包场景。 |
| 无 RX 完成、无 TX 尝试 | 与手机侧激活前置条件相容，但不能证明它。端点特有的调度/DMA 故障仍有可能；token 不能代替 USB 握手抓包。 |

独立 short-IN HCD 补丁仍是经过测试的通用修正。当前板端证据不足以把未完成 RX 归因于该问题，也不能依据误认地址继续添加队列/cache 补丁。

## 下一步验证：保持驱动与激活独立

要求再次烧录前，先使用已有 V7 镜像建立宿主激活证据。现有应用命令可测试 USBMUX 及其外部配对/会话层：

```text
carplay_usb mux
carplay_usb pair
ipheth_status
usb_ehci_status 1
```

解锁手机，并在出现信任对话框时响应。记录确切的 USBMUX 协商和配对/会话阶段，不能只记录端点绑定。如果已有会话打开，无需再次配对；不要仅为执行此检查清除信任记录或关闭会话。应用 pair 命令还会在会话建立后尝试应用服务；后续服务失败不等于缺少 Ethernet 信任，此服务也不是 IPHETH 的要求。如果 -4 超时仍发生在较早的 USBMUX/Lockdown 阶段，应独立调查外部依赖，不在驱动中实现配对。

若有 Linux 参考宿主，可使用同一手机和线缆，以当前 ipheth + usbmuxd/libimobiledevice 采集基线。用 usbmon 比较接口 class/配置、vendor request、成功会话建立、首次 Bulk IN 和网络流量。不同配置下个人热点成功，可作为设备/线缆基线，但不能证明 CarPlay 配置行为或 D213 信任。不得假定 Linux 配对记录适用于新的 D213 身份。使用可移植驱动不强制要求 PC 基线，但它比不断改变板端 alt 或 NCM 缓冲区大小提供更强证据。

## 置信度与验证限制

普通 CTest 和 ASan/UBSan 测试覆盖 TX 修正在 14、42、60、64、70、512 和 1514 字节时的行为、精确容量、字节保持及提交长度之后的 canary。Class 测试要求 NCM 和 legacy 两种模式都实际提交 42 字节 USB OUT。历史 70 字节超时不会因填充修正改变。后续 v13 已证明硬件 RX/TX，见下文；重连压力仍未证明。

社区 [libimobiledevice #299](https://github.com/libimobiledevice/libimobiledevice/issues/299) 支持外部配对的职责边界；它是旧讨论，不是当前协议规范。[usbmuxd #285](https://github.com/libimobiledevice/usbmuxd/issues/285) 报告据称成功配对后 Linux 仍为 NO-CARRIER。该未核实报告提醒我们，仅配对成功不足以证明 Ethernet 就绪；不假定其与当前手机/型号/iOS 版本相同。

## 后续 v13 硬件证据

用户提供的 D213 运行完成外部宿主激活，报告 RX=79、TX=18、81 次接收完成、ready=yes、carrier=up。DHCP 分配 172.20.10.4/28，gateway/DNS 为 172.20.10.1。后续应用流量到达已公布的有线 IPv6 RTSP 端点。这取代了早先零 RX 状态，但配对/会话代码仍不属于本组件。应用协议接受及媒体启动是独立验证层。

一次 RX 错误仍未分类，没有记录到 USB 或 TX 错误。持续流量和物理重插压力仍待验证；该证据只覆盖一个已观察的手机/配置。v7 到 v13 间的应用传输变更记录在应用自己的验证文档中。一次成功运行不能证明早先间歇性传输故障只有某个单一原因。
