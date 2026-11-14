[English](../sources.md) | [简体中文](sources.md)

# 源码与协议来源

研究日期：2026-11-10。参考文件位于本仓库之外；本仓库不包含完整 USB 栈或操作系统 fork。

| 参考资料 | 审查版本 | 确认内容 |
| --- | --- | --- |
| [Linux ipheth.c](https://github.com/torvalds/linux/blob/067c5c24cef23d913b67eaaf71ae67e7169a6620/drivers/net/usb/ipheth.c) | 记录的 Linux master HEAD 为 `067c5c24cef23d913b67eaaf71ae67e7169a6620` | ff/fd/01、vendor command 00/04/45、legacy RX 前缀、仅 RX 使用 Apple NCM、原始 TX、两种 carrier 回复格式、carrier/TX 前首次成功 RX 的就绪门控、空包和控制 RX 过滤 |
| [FreeBSD if_ipheth.c](https://github.com/freebsd/freebsd-src/blob/2d5a6bc2a4c5eb061f8d63b7aaff7dc04e96f1ea/sys/dev/usb/net/if_ipheth.c) | 记录的 main HEAD 为 `2d5a6bc2a4c5eb061f8d63b7aaff7dc04e96f1ea` | 请求 index 来自发现的接口、alternate setting、独立 Ethernet 连接、包校验 |
| [CherryUSB](https://github.com/cherry-embedded/CherryUSB/tree/a0054795c44037ace8a2b77b6e610adbff2257ea) | `a0054795c44037ace8a2b77b6e610adbff2257ea` | 外部 class API、URB、OSAL、段注册 |
| [Luban-Lite SDK](https://github.com/jiachenin/luban-lite-jc) | 本地 `poc/carplay-oss`，基线 `d14f4803d` | 原厂 1.5 控制返回值契约差异；升级为官方 a0054795 并使用独立 SDK overlay；RT-Thread/lwIP 2.1.2、class KEEP 和 SCons 集成 |
| [RT-Thread](https://github.com/RT-Thread/rt-thread) | SDK 引入的 `kernel/rt-thread/` | eth_device 注册、链路通知、netif 生命周期、pbuf 与 IPv6 初始化 |
| [rt-carplay](https://github.com/boa-z/rt-carplay) | 本地 main，初始 `db086d1` | 仅参考应用侧配置选择与公开网络集成；未向驱动复制源码 |

Linux 和 FreeBSD 文件从各自分支的 raw URL 下载；上述 HEAD 在同一研究会话中记录。下载文件的精确 SHA-256：

```text
Linux:   b2afefacb501e91ccab18b0bd6ca52767588d351701256afeae8a53088ba1d78
FreeBSD: e50e6f3ba7ce63c6304c3cdbc98bf5a2fbc2aa67b87ccfa9f68d94653bacef6b
```

新增实现文件采用 Apache-2.0。Linux ipheth 除 GPL-2.0-only 外也提供 BSD-3-Clause 许可选项；本项目按 BSD 条款参考其行为，原始声明保留在 `LICENSES/BSD-3-Clause-ipheth.txt`。FreeBSD 声明保留在 `LICENSES/BSD-2-Clause-if_ipheth.txt`。源码为新编写的实现，没有复制 Linux/FreeBSD 驱动，也没有重新许可 USB 栈。第三方原始声明与新增代码许可分别保留。

有意保留的差异包括：动态发现接口/端点/alternate；不使用 PID 列表，不假定接口为 2；交付前完整校验 NCM；显式校验长度、重叠和终止项；独立设备资源；协作停止和取消失败隔离。TX 遵循当前 Linux 及观察到的 Apple 驱动行为，按原始 Ethernet 长度提交，不增加尾部填充。未采用 FreeBSD 固定填充至 1514 字节的 TX 行为。协议审计检查 Linux 提交 3e65efcca87a 后，删除了早期最小 60 字节填充。

## 社区检索

GitHub 检索未发现以下匹配：

- `cherry-embedded/CherryUSB` 中的 `ipheth` 代码及 issue/PR。
- 该仓库中包含 `"Apple Ethernet"` 的 issue/PR。
- 同时包含 `CherryUSB` 和 `ipheth` 的全局代码/仓库检索。
- `RT-Thread/rt-thread` 中的 `IPHETH` 代码检索。

这些是当时的检索记录，不代表不存在私有实现、未索引补丁、其他拼写或外部下游实现。提议上游 PR 前应重新检索。

## 上游准备

驱动使用外部 Class Driver API，不要求 CherryUSB 核心补丁。协议代码与 RT-Thread 代码分离，核心不依赖硬件或业务 API。项目提供固定版本头文件测试、sanitizer 测试、来源说明、移植和硬件验证步骤。公开仓库为 https://github.com/boa-z/cherryusb-ipheth；既有变更已获得仓库所有者授权提交并推送。

上游提交前还需：采集两种模式的真实 iPhone RX/TX；在目标 HCD 验证 DMA/cache 和断开压力；覆盖多个手机型号/配置；与维护者讨论内存开销和生命周期策略；按上游偏好调整目录与构建风格。既有公开 CI 的普通和 ASan/UBSan 矩阵任务均已通过。机制审计和证据边界见 [protocol-audit.md](protocol-audit.md)。
