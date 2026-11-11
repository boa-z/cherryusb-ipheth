# Validation record and board procedure

## Automated results (2026-11-10)

- CMake/CTest Debug: protocol, class lifecycle and actual upstream EP0 tests passed.
- CMake/CTest ASan + UBSan + default leak detection: all three tests passed against
  official CherryUSB commit a0054795 as vendored by the upgraded SDK.
- `-Wall -Wextra -Werror` used for host sources/tests.
- Protocol tests: descriptor truncation/mutation, non-Apple VID, nonfixed
  interface 7/alternate ordering, MAC/carrier reply variants, legacy limits,
  NCM overlap/truncation/termination/chained-table rejection, 64 KiB boundary,
  callback failure, TX bounds/padding and 50,000 malformed random inputs.
- Class tests: allocation/MAC/NCM/net/thread failure unwind, NCM and legacy
  negotiation, failed/short TX, 20 reconnect cycles, RX/TX STALL recovery,
  concurrent TX during disconnect, two simultaneous devices/slot exhaustion,
  failed cancellation quarantine and a late callback after detach.
- Real upstream `usbh_control_transfer`: zero-data success, 6/64-byte MAC
  replies, carrier byte, incomplete Setup accounting, STALL retry, timeout,
  disconnected device and invalid arguments. This specifically guards the
  length contract that mocked class tests alone did not exercise.
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
SHA256 02d34f3233b26d91e2c2bbe41b629a1705347f1cf28baef14af0d9f4bc32a174
```

The unique v2 test artifact is
`/home/boa/Documents/carplay-test-images/2026-11-10-cherryusb-1.6.1-v2/d213-carplay-ipheth-cherryusb-1.6.1-v2.img`.
It contains official CherryUSB 1.6.1 at a0054795. The previous image (SHA256
starting 5df9aa72) must not be used for this retest.

The firmware builds with USB1 host and IPv6. The current image's startup says
`rt-carplay: run carplay_start, then carplay_status ...`; an earlier Phase 1
hardware-probe banner is a different application/image. Confirm `ipheth_status`
exists before collecting network results. Board reports have confirmed the new
entry banner, correct configuration selection, USBMUX binding and IPHETH class
matching. The first attempt then failed with -2 during Ethernet initialization
and the old enumerator detached USBMUX. The old control-length contract was
identified as the cause; the official-stack v2 image and inactive-class failure
handling address it. Successful vendor requests and actual network traffic
remain pending hardware retest. No local board/serial access is available.

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
GET_MAC should report data bytes only (6 or a padded reply up to 64), and
successful ENABLE_NCM should return 0. A STALL may select legacy mode. Any
other initialization failure should log its step and leave USBMUX attached;
collect that diagnostic before interpreting the network state.

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
