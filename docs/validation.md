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
SHA256 9897fa7d48fbbc07bf4dc19320b2bdf39c79d9f7644bbccbdc8a2744341d3af1
```

The unique v6 test artifact is
`/home/boa/Documents/carplay-test-images/2026-11-10-cherryusb-1.6.1-v6/d213-carplay-ipheth-cherryusb-1.6.1-v6.img`.
It contains official CherryUSB 1.6.1 at a0054795 and application DMA fix
`382b293`, plus IPHETH readiness correction and RX diagnostics `1283913`. V6
adds SDK EHCI short-IN correction `88ef9b599`, has a complete firmware build
and awaits board testing. Both plain and
ASan/UBSan host suites passed all three tests with this code. The previous image (SHA256
starting 5df9aa72) must not be used for this retest.

The firmware builds with USB1 host and IPv6. The current image's startup says
`rt-carplay: run carplay_start, then carplay_status ...`; an earlier Phase 1
hardware-probe banner is a different application/image. Confirm `ipheth_status`
exists before collecting network results. Board reports have confirmed the new
entry banner, correct configuration selection, USBMUX binding and IPHETH class
matching. The first attempt then failed with -2 during Ethernet initialization
and the old enumerator detached USBMUX. The old control-length contract was
identified as the cause; the official stack and inactive-class failure
handling address it. The v2 hardware attempt then stopped earlier in application
configuration scanning: EHCI asserted because the application submitted an
unaligned stack Setup packet / descriptor buffer. The v3 application copies
control and USBMUX buffers through owned, cache-line-aligned/padded DMA storage.
Official core and EHCI assertions remain unchanged. The actual application port
is now compiled by a host ASan/UBSan regression covering unaligned input, short
replies, bounds, allocation failures, timeout cancellation and late callbacks.
V2 (SHA256 starting 02d34f32) is superseded. V3 subsequently enumerated and
bound successfully; its board result is recorded below. Actual network traffic
remains pending. No local board/serial access is available.

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

USB trace captures of vendor request/reply contents (UART confirms MAC/NCM
request success); on-wire IPv6 DAD and peer response; Ethernet RX/TX and scoped
IPv6 connectivity; high-rate cache behavior; disconnect/reconnect and USBMUX coexistence under load; alternate
phone models and legacy mode. Host mocks/ELF registration do not establish these.

## V3 board result and TX diagnosis (2026-11-10)

User-supplied UART logs from the v3 image confirm:

- Configuration index 3/value 4 selected; USBMUX attaches on interface 1 and
  remains attached while IPHETH binds interface 2, alternate 1, IN86/OUT05.
- GET_MAC returns 6 data bytes and ENABLE_NCM returns 0; NCM RX selected.
- RT-Thread i0 registers, carrier is UP, and lwIP reports its Link-Local
  address VALID. This local state alone does not prove peer reachability or
  that DAD packets were transmitted successfully.
- Snapshot: RX=0 TX=0 rxerr=0 txerr=10 usberr=0. No successful Ethernet frame
  transfer is yet demonstrated. The existing usb_errors counter covers the
  receive/worker control path, not TX submission failures.

New `ipheth_status` TX diagnostics distinguish `frame` (protocol validation),
`submit` (negative HCD return) and `short` (success with an unexpected OUT
length). `last` is the last failed result, `length` the original Ethernet
length, and `actual` the HCD OUT length. Last failure details persist after
successful traffic. Error number meanings depend on the category: protocol
errors and CherryUSB errors use separate namespaces. Host tests assert each
category. Collect two status snapshots five seconds apart before modifying
phone settings or attributing the errors to pairing.

Pairing/trust remains external; no pairing changes are part of this diagnosis.
A DHCP-enabled flag with IPv4 0.0.0.0 does not establish a DHCP service on the
phone. IPv6 testing must use a known peer address and interface scope.

## Published validation

The initial public main push (`1518ceb`) completed both GitHub Actions host
matrix jobs (plain and ASan/UBSan):
https://github.com/boa-z/cherryusb-ipheth/actions/runs/42955915884

## V4 board result and first-RX readiness correction

The v4 UART snapshot has RX=0 TX=0 txerr=12, with frame=0 submit=12
short=0 last=-14 length=70 actual=0. In the pinned official CherryUSB,
-14 is USB_ERR_TIMEOUT. Since the adapter uses synchronous TX submission,
this is a completion wait timeout, not descriptor validation or a short OUT.
This does not prove whether the device NAKed, external trust is missing, or
controller scheduling/completion is at fault; no USB trace is available.

Rechecking Linux ipheth shows that confirmed_pairing is set after the first
successful Bulk IN completion (including empty/control transfers), and carrier
checks are skipped until then. This driver previously missed that readiness
gate and allowed lwIP to transmit based on a carrier-on reply alone. V5 adopts
the Linux behavior, filters empty and four-byte 00 01 xx xx control transfers,
and exposes Bulk IN submission/success/byte/control counters and RX readiness.
TX before readiness/carrier-up is rejected locally and counted as deferred.
This corrects premature link-up; it does not establish the cause of the phone's
lack of received data or claim that pairing/USB traffic is now fixed.

Tests cover carrier polling suppression before RX, local TX deferral without
USB errors, empty/control completion readiness, no false Ethernet delivery or
parse error, link-up after readiness, and reconnect state reset. Prior TX,
STALL, concurrent unplug and quarantine cases remain enabled.

On the next board run collect `ipheth_status`, `ifconfig` and `carplay_usb net`
after attach and again five seconds later. With no Bulk IN completion, the
expected state is ready=no, carrier=down and no IPHETH Bulk OUT submissions.
Local Link-Local VALID alone continues to be insufficient evidence of traffic.
If readiness remains absent, investigate external trust and controller/USB
captures independently; do not add pairing protocols to the Ethernet driver.

## V5 board observation and SDK EHCI reproduction (v6)

V5 UART: i0 attached, RX/TX/errors all zero, submitted=1 completions=0,
ready=no, carrier=down, LINK_DOWN. This confirms the readiness gate is working;
it does not establish whether the phone is silent or a completion was lost.

Inspection of the actual official EHCI HCD revealed a separate reproducible
controller defect. The 64 KiB IN request becomes four 16 KiB qTDs. A short
packet in an early qTD follows its terminating alternate link and leaves the
later qTDs Active. The old scanner still waits for every later qTD to finish;
only the final qTD has IOC. A software descriptor/MMIO reproduction using the
actual original HCD confirms a four-byte first transfer remains pending.

SDK commit `88ef9b599` adds a standalone unified patch and original/patched
ASan/UBSan regressions under `cherryusb-luban/`. The build patches a generated
HCD copy; all 933 official source files stay byte-identical to a0054795. The
patch enables IOC on each bulk IN qTD, recognizes terminal short IN/errors,
counts only completed descriptors, and refreshes the overlay data toggle.
Control IN short data still waits for the status stage. Tests cover zero/short
first or middle IN, complete IN/OUT, pending chains, halted qTD, cache hooks,
data toggle and control status. Original reproduction and patched tests passed;
D213 firmware linked and packaged. No IPHETH framing/receive-size workaround or
pairing change is part of this HCD correction. This patch is prepared for
upstream review; no PR has been opened.

V6 must still demonstrate real RX completion. Collect the same status snapshots
and USBMUX status. A successful hardware result is not implied by a software
reproduction; if completions remain zero, external activation/trust and USB
traces/controller state remain independent investigation paths.
