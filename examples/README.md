[English](README.md) | [简体中文](README.zh-CN.md)

The USB class needs a network adapter implementing the callbacks declared in `include/usbh_ipheth.h`, plus `usbh_ipheth_now_ms()`. For RT-Thread/lwIP use `port/rtthread/ipheth_rtthread.c` and define `IPHETH_EXTERNAL_NET_ADAPTER`.

The USB host initialization and USB configuration selection belong to the application. Once enumeration selects a configuration containing Apple ff/fd/01, the class registers i0 (and i1 for a second phone) automatically. An existing USBMUX class continues to own ff/fe/02 independently.

Applications use ordinary IPv4/IPv6 sockets, selecting the i0 address/interface where necessary. Pair/trust the phone outside this component before expecting traffic. No Lockdown, MFi, or CarPlay application library is required to build.

Static archive integrations must call `usbh_ipheth_link_anchor()` during app initialization, or use an OBJECT/direct source/whole-archive link. The linker script must KEEP(*(.usbh_class_info)) (or usbh_class_info on the native toolchain).
