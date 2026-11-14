[English](README.md) | [简体中文](README.zh-CN.md)

# cherryusb-ipheth

## Project overview

An external Apple iPhone USB Ethernet host class for CherryUSB. The protocol core is C99 and independent of USB libraries, operating systems, and network stacks. The included network adapter targets RT-Thread with lwIP 2.x.

Host tests and D213 cross-builds pass. User-supplied v13 board logs confirm composite USBMUX/IPHETH binding, MAC retrieval, Apple NCM RX negotiation, bidirectional Ethernet traffic and DHCP (172.20.10.4/28, gateway 172.20.10.1) after external host activation. The interface has a valid IPv6 Link-Local address; subsequent application logs show RTSP requests over the advertised wired IPv6 endpoint. The driver waits for a successful Bulk IN before link-up, following Linux IPHETH. One RX error was recorded; its cause and sustained traffic/unplug/replug stability remain to be validated. See [validation](docs/validation.md) and the [protocol audit](docs/protocol-audit.md). Pairing/trust is an external prerequisite; this project implements no pairing, authentication, application session, or device-wide USB configuration policy.

## Features

- Apple VID plus ff/fd/01 interface recognition, independent of PID, configuration value, interface number, and bulk endpoint addresses.
- Descriptor-based alternate setting selection and private endpoint ownership.
- Vendor MAC, NCM enable and carrier requests; legacy fallback only when the NCM enable request stalls (transport errors fail binding).
- Legacy RX (two-byte prefix), Apple NCM16 RX, and raw Ethernet TX in both modes.
- MTU 1500, MAC validation, carrier polling, error counters, RX/TX STALL recovery.
- Per-device DMA storage, bounded TX, cooperative worker shutdown, failed cancellation quarantine, and persistent network devices for safe reconnect.
- RT-Thread Ethernet devices i0/i1 with lwIP IPv6 Link-Local/DAD support.

Apple NCM RX does **not** require a standard CDC-NCM descriptor and does not make TX an NCM transfer. This class does not claim USBMUX or select a different USB configuration.

## Driver architecture

The C99 protocol core validates descriptors, vendor replies, Legacy RX and Apple NCM16 blocks without allocating memory or calling an OS. The CherryUSB class owns USB resources and a per-device worker. A separate network adapter copies received Ethernet frames into the host stack. Applications own USB configuration selection and any activation or pairing layer. See [architecture](docs/architecture.md) for callback contexts and lifetime rules.

## Protocol support

Both RX modes use raw Ethernet TX with the caller's original frame length. The supported frame range is 14..1514 bytes with MTU 1500. NCM16 supports the observed Apple NTH/NDP layout, up to 22 datagrams, 64 KiB blocks, no chained NDPs and no CRC. Full validation precedes frame delivery. VLAN, jumbo frames, NTH32, standard CDC-NCM class binding and NCM TX are outside this implementation. See the [protocol audit](docs/protocol-audit.md) before changing framing or activation assumptions.

## Compatibility

The pinned Host reference is official CherryUSB 1.6.1 commit `a0054795c44037ace8a2b77b6e610adbff2257ea`. RT-Thread integration targets lwIP 2.x; D213 cross-builds use lwIP 2.1.2. Device matching uses Apple VID and ff/fd/01, not a PID list. Hardware evidence covers the supplied iPhone 05ac:12a8 and configuration value 4; it does not establish all-model compatibility. See [compatibility](docs/compatibility.md).

## Build and test

Use a C99 compiler and CMake 3.16 or newer. Full class tests additionally need a C11 compiler, POSIX threads and an external CherryUSB checkout. Sanitizer tests require GCC/Clang with AddressSanitizer and UndefinedBehaviorSanitizer support. Documentation checks use Python 3 only and make no network requests.

Only the protocol core requires no external headers:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

To compile and exercise the actual class against an external CherryUSB checkout:

```sh
cmake -S . -B build-san -DCMAKE_BUILD_TYPE=Debug \
  -DIPHETH_SANITIZE=ON -DCHERRYUSB_ROOT=/path/to/CherryUSB
cmake --build build-san
ASAN_OPTIONS=detect_leaks=1 ctest --test-dir build-san --output-on-failure
```

`make check sanitize` and `make check-class sanitize-class CHERRYUSB_ROOT=/path/to/CherryUSB` are alternatives. No CherryUSB fork is vendored.

Run the bilingual documentation checks separately:

```sh
python3 scripts/check-docs.py
```

## CherryUSB integration

Compile `src/ipheth.c` and `port/cherryusb/usbh_ipheth.c`, add `include/` and the selected CherryUSB core/common/hub/config include paths, and provide the callbacks in `include/usbh_ipheth.h` plus `usbh_ipheth_now_ms()`. The weak network attach rejects binding until an adapter is provided. `integration/` supplies build helpers; see [porting](docs/porting.md) and [examples](examples/README.md).

Direct source/OBJECT linking is preferred. For a static archive, call `usbh_ipheth_link_anchor()` or use whole-archive, and retain `KEEP(*(.usbh_class_info))` in the linker script. The HCD must support 64 KiB multi-qTD RX, short IN completion, bounded synchronous transfers and synchronous successful cancellation. The D213-specific EHCI short-IN correction belongs to the SDK overlay, not this portable driver.

## RT-Thread / lwIP integration

Also compile `port/rtthread/ipheth_rtthread.c` and define `IPHETH_EXTERNAL_NET_ADAPTER`; `integration/SConscript` selects this when `RT_USING_LWIP` is enabled. Start the TCP/IP thread before USB enumeration. Enable `RT_USING_LWIP_IPV6` for IPv6. The adapter registers persistent i0/i1 devices, copies frames into pbufs, updates MAC through the lwIP thread, and restarts Link-Local DAD on attachment. It does not choose a default route. Applications use ordinary scoped IPv6 or IPv4 sockets.

## USB and network lifecycle

The host selects the USB configuration before class binding. The driver claims only the discovered Ethernet interface, selects its alternate, reads MAC and negotiates RX mode. Its worker waits for the first successful Bulk IN completion before Carrier polling/link-up; empty/control completions can establish RX readiness without delivering an Ethernet frame. Readiness is not pairing or authentication.

TX is synchronous with the existing 1000 ms timeout and a private mutex; one RX URB is outstanding. Detach stops the worker, cancels RX, reports link down, drains network TX users, and releases interface resources. Failed HCD cancellation quarantines the slot until reboot. Persistent RT-Thread netifs reject detached TX and are reused on replug. Callers must serialize public send/stats/probe operations against detach and cannot retain RX views after callbacks. See [architecture](docs/architecture.md).

## Diagnostics

`ipheth_status` reports attachment, Carrier/RX readiness, frame and USB counters, TX failure categories and last failure details. `ifconfig` shows actual addresses and flags. The optional low-priority `ipheth_performance_report()` samples timing windows; FINSH `ipheth_probe carrier off` / `ipheth_probe carrier on` controls a diagnostic experiment without bypassing initial readiness. See [latency probe](docs/latency-probe.md) for timing definitions and RX error classification. USBMUX/Lockdown diagnostics remain application responsibilities.

## Hardware validation

| Evidence level | Result and scope |
| --- | --- |
| Host Tested | Protocol, actual class lifecycle and upstream EP0 contract tests; plain and ASan/UBSan |
| Cross-Compiled | Complete D213 RT-Thread/lwIP firmware linked and packaged |
| Hardware Validated | Historical supplied v13 UART: composite binding, NCM RX/raw TX, DHCP, gateway ICMP and valid local IPv6; later application RTSP observed |
| Pending Validation | Current documentation revision hardware regression, sustained load, physical replug stress, peer IPv6/NDP, legacy-mode hardware and additional phones |

See [validation](docs/validation.md) for historical image identities, exact logs, procedures and unresolved RX errors. Host tests and cross-compilation do not establish DMA/cache correctness or current-board acceptance.

## Known limitations

Two default device slots each reserve a 64 KiB RX buffer plus aligned control/TX storage. Failed cancellation intentionally consumes a slot until reboot. Other HCDs require independent cache, timeout, short-transfer and cancellation validation. Interface devices remain registered while unplugged. A failed initialization needs re-enumeration after resolving prerequisites. Pairing, trust, USBMUX, Lockdown, MFi, CarPlay sessions and device-wide configuration policy are external. The recorded v13 RX errors remain unclassified; the later diagnostic counters do not themselves identify a cause.

## Documentation

English is primary; each maintained document has a complete Simplified Chinese counterpart and navigation at the top. Keep paragraphs and list items on one physical line; preserve code/log line breaks. The local checker verifies pairs, navigation, relative file/heading links, naming, layout and mechanical translation drift indicators. It does not prove semantic translation accuracy. Original third-party legal notices in `LICENSES/` remain unmodified and untranslated; generated build output is not maintained documentation.

| Document | Purpose |
| --- | --- |
| [Architecture](docs/architecture.md) | Ownership, callbacks and teardown |
| [Compatibility](docs/compatibility.md) | Versions, protocol scope and device observations |
| [Latency probe](docs/latency-probe.md) | Timing windows, Carrier experiment and RX diagnostics |
| [Porting](docs/porting.md) | Build, DMA, OSAL, network and HCD contracts |
| [Protocol audit](docs/protocol-audit.md) | Mechanism evidence and activation boundary |
| [Sources](docs/sources.md) | Provenance, licenses and upstream preparation |
| [Validation](docs/validation.md) | Test results, historical images and board procedure |
| [Examples](examples/README.md) | Application integration requirements |
| [Documentation audit](docs/documentation-audit.md) | Bilingual inventory, exceptions and local verification |

Repository layout:

| Path | Purpose |
| --- | --- |
| `include/ipheth.h`, `src/` | Descriptor, request and packet core |
| `include/usbh_ipheth.h`, `port/cherryusb/` | Host class and network callback seam |
| `port/rtthread/` | Ethernet device, lwIP pbuf, IPv6, status command |
| `integration/` | SCons and CMake helpers |
| `tests/` | Malformed packets and mocked real-class lifecycle |
| `examples/` | Minimal application integration instructions |
| `docs/` | Architecture, porting, compatibility, validation, source licenses |

## License

New code is Apache-2.0; see [LICENSE](LICENSE). Protocol references and their retained BSD notices are listed in [sources](docs/sources.md) and `LICENSES/`. Original third-party notices are preserved separately; this project does not relicense CherryUSB, RT-Thread or a copied operating-system driver.

## Acknowledgments

Thanks to CherryUSB and RT-Thread/lwIP for the host and network infrastructure, and to the Linux and FreeBSD IPHETH contributors for documented behavior and protocol history. The [source record](docs/sources.md) identifies inspected revisions and retained notices. Thanks also to the contributors supplying D213 UART evidence; those observations remain scoped to the recorded artifacts and devices.
