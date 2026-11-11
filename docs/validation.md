# Validation record and board procedure

## Automated results (2026-11-10)

- CMake/CTest Debug: protocol and actual class tests passed.
- CMake/CTest ASan + UBSan + default leak detection: both tests passed against
  upstream CherryUSB commit a0054795 and the SDK-vendored CherryUSB 1.5 headers.
- `-Wall -Wextra -Werror` used for host sources/tests.
- Protocol tests: descriptor truncation/mutation, non-Apple VID, nonfixed
  interface 7/alternate ordering, MAC/carrier reply variants, legacy limits,
  NCM overlap/truncation/termination/chained-table rejection, 64 KiB boundary,
  callback failure, TX bounds/padding and 50,000 malformed random inputs.
- Class tests: allocation/MAC/NCM/net/thread failure unwind, NCM and legacy
  negotiation, failed/short TX, 20 reconnect cycles, RX/TX STALL recovery,
  concurrent TX during disconnect, two simultaneous devices/slot exhaustion,
  failed cancellation quarantine and a late callback after detach.
- D213 complete firmware linked and packaged from SDK branch poc/carplay-oss.
  ELF inspection confirms both `ipheth_class_info` and `carplay_usbmux_class_info`
  inside the retained class section, plus `ipheth_status`.
- rt-carplay host tests and its platform-boundary check passed. The latter
  filters C/H files so SDK-generated object files do not produce false matches.

These tests do not emulate USB electrical behavior, HCD cache coherency, Apple
trust state or real TCP/IPv6 interoperability.

## D213 image

SDK worktree: `/home/boa/Documents/luban-lite-jc-carplay-oss`.
External component: sibling checkout `../cherryusb-ipheth`.
External application: sibling checkout `../rt-carplay`.

```sh
scons --apply-def=d21x_demo128-nand_rt-thread_rtcarplay_defconfig
scons -j8
```

Image:

```text
output/d21x_demo128-nand_rt-thread_rtcarplay/images/d21x_demo128_nand_carplay_page_2k_block_128k_v1.0.0.img
SHA256 5df9aa72b3a3e8d2548dca74363c76615744ae626d70061fcf78b5a11db8a351
```

The firmware builds with USB1 host and IPv6. The current image's startup says
`rt-carplay: run carplay_start, then carplay_status ...`; an earlier Phase 1
hardware-probe banner is a different application/image. Confirm `ipheth_status`
exists before collecting network results. Board reports have confirmed the new
entry banner and basic iPhone high-speed enumeration; Apple Ethernet binding
and network traffic are still pending.

## Board procedure (115200, 8N1)

Start with the phone disconnected, then:

```text
carplay_usb start
```

Plug the unlocked phone into USB1. Starting before insertion matters because
the application enables its configuration-selection hook at this point. If the
phone was already inserted and only PTP (06/01/01) enumerated, unplug/replug
following start. This driver itself never changes USB configurations.

Capture all enumeration logs, then:

```text
carplay_usb status
ipheth_status
ifconfig
carplay_usb net
```

Expected supplied-phone descriptors: configuration index 3/value 4, USBMUX on
interface 1, Apple ff/fd/01 on interface 2. Driver log should identify selected
alternate and bulk endpoints (observed alt 1, IN 86 OUT 05); `i0` must register.
These expected observations are not binding rules for other devices.

Pairing/trust is external to the driver. Resolve it through an existing trusted
host/application before interpreting no-carrier or silent traffic as a framing
failure. Do not add pairing code here; record USBMUX/Lockdown failures separately.

After five seconds repeat `ipheth_status`, `ifconfig`, `carplay_usb net`.
Check carrier, Link-Local tentative -> valid DAD, RX/TX counter increments, and
error counters. Once the peer Link-Local address is known, test ICMPv6 from the
peer to the board's address with the peer's USB interface scope. From a Linux
peer, the equivalent is `ping -6 <board-link-local>%<peer-usb-interface>`;
an iPhone needs an IPv6-capable diagnostic app/endpoint. Do not guess the peer
address. Capture NDP/ICMPv6 and both counter snapshots. If the board image lacks
an IPv6 ping command, use a peer or a normal scoped AF_INET6 socket test.

Then unplug and query `ipheth_status`/`ifconfig`: the persistent i0 should remain
registered, detached and link down. Replug: exactly one adapter for that slot,
updated MAC if changed, fresh DAD and resumed traffic. Repeat at least 100 times,
including an active stream; verify no crash, stale callback, steadily growing
heap/thread count, or disturbance to USBMUX. Test cable removal during TX/RX,
trust refusal, RX/TX STALL, and a second phone if available.

For an application session, proceed only after IPHETH networking passes; the
application uses sockets and the configured public i0 interface. This stage's
network verification does not require starting an application receiver session.

## Still requiring hardware evidence

Actual vendor request/reply captures; chosen NCM vs legacy mode; valid MAC;
carrier and IPv6 DAD; Ethernet RX/TX and scoped IPv6 connectivity; high-rate
cache behavior; disconnect/reconnect and USBMUX coexistence under load; alternate
phone models and legacy mode. Host mocks/ELF registration do not establish these.
