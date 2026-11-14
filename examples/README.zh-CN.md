[English](README.md) | [简体中文](README.zh-CN.md)

USB class 需要一个网络适配器，实现 `include/usbh_ipheth.h` 声明的回调以及 `usbh_ipheth_now_ms()`。使用 RT-Thread/lwIP 时采用 `port/rtthread/ipheth_rtthread.c`，并定义 `IPHETH_EXTERNAL_NET_ADAPTER`。

USB host 初始化与 USB 配置选择由应用负责。枚举选中包含 Apple ff/fd/01 的配置后，class 会自动注册 i0，第二部手机使用 i1。已有 USBMUX class 继续独立持有 ff/fe/02。

应用使用普通 IPv4/IPv6 socket，必要时选择 i0 的地址或接口。预期传输流量前，应在本组件外完成手机配对和信任。构建本组件不要求 Lockdown、MFi 或 CarPlay 应用库。

静态库集成必须在应用初始化期间调用 `usbh_ipheth_link_anchor()`，或使用 OBJECT、直接源文件或 whole-archive 链接。链接脚本必须保留 KEEP(*(.usbh_class_info))，原生工具链也可能使用 usbh_class_info。
