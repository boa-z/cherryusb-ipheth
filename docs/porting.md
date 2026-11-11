# Porting

Compile `src/ipheth.c` and `port/cherryusb/usbh_ipheth.c`, add `include/`, plus
CherryUSB's core/common/hub/config include paths. For RT-Thread/lwIP also compile
`port/rtthread/ipheth_rtthread.c` and define `IPHETH_EXTERNAL_NET_ADAPTER`.
`integration/SConscript` selects this adapter when `RT_USING_LWIP` is enabled;
the integrating package supplies the selected CherryUSB include paths.

Other network stacks implement the attach/detach/receive/link callbacks and the
monotonic millisecond clock declared in `usbh_ipheth.h`. Without an adapter the
weak attach callback rejects the bind. The clock wraps naturally at 32 bits.
RT-Thread's implementation derives milliseconds from its tick counter.

Applications must initialize the host and choose the desired configuration
before class binding. This component never changes a device's configuration,
requests application-specific modes, performs pairing, or owns another
interface. A phone may need pairing/trust before the initial MAC request works;
a failed bind then requires re-enumeration after satisfying that prerequisite.

Static archive users must call `usbh_ipheth_link_anchor()` or use whole-archive
linking. Direct source/OBJECT linkage is preferred. The linker must retain
`KEEP(*(.usbh_class_info))` (or `usbh_class_info` for that toolchain). An empty
anchor declaration alone does not make the linker extract an archive member.

## DMA and OSAL contract

The two default slots each reserve a 64 KiB RX buffer plus aligned TX, control
and setup buffers. All occupy complete `CONFIG_USB_ALIGN_SIZE` cache lines in
`USB_NOCACHE_RAM_SECTION`. The HCD must clean OUT/setup and invalidate IN data,
or the platform must map that section uncached. Do not put this storage in
memory inaccessible to the controller. Platform linker/memory/cache policy is
outside the portable protocol core. Test real high-rate transfers on each port.

The HCD must preserve URB data toggles across transfers and synchronously
quiesce callbacks and DMA on successful `usbh_kill_urb`. A failed kill quarantines
its slot. Bounded synchronous control/TX submission must return only when their
DMA is quiescent, including timeout/error paths. The SDK EHCI implementation
was inspected, but its physical cache and cancellation behavior still needs
board testing. A broken HCD timeout implementation cannot be repaired here.

OSAL semaphores must synchronize completion memory across contexts. Critical
sections must exclude worker/interrupt accesses (and all cores on SMP). Class
connect/disconnect callbacks for a given hport must be serialized. Configure
`CONFIG_USBHOST_IPHETH_MAX_DEVICES` (default 2; RT device suffixes support at most
10), worker priority (default 12), and stack (4096 bytes) as needed.

The 64 KiB RX uses multiple EHCI qTDs; reserve room for concurrent RX, TX, EP0
and other interfaces. The SDK supports chained 16 KiB qTDs. Do not assume that
other CherryUSB HCDs support this size or that all controllers are cache coherent.

RT-Thread Ethernet devices persist as i0/i1 when detached. Registration uses
RT-Thread's normal Ethernet initialization; lwIP handles ARP, ICMPv6, Neighbor
Discovery, DAD, DHCP and IP routing. Enable `RT_USING_LWIP_IPV6` for IPv6. The
status command is `ipheth_status`; `ifconfig` shows network addresses and flags.
The RT device's two-character name can differ from lwIP's name plus numeric
index; consumers should query the actual interface instead of assuming index 0.

## EHCI multi-qTD short IN

The 64 KiB NCM request requires an HCD that completes a bulk IN URB when an
early qTD ends with a short packet, even if following qTDs remain Active. The
pinned a0054795 EHCI implementation has a reproduced defect in this path. The
D213 SDK carries a separate tested controller patch; it is not bundled with
this portable driver. See the v6 record in validation.md. Do not shrink NCM
buffers or fabricate standard CDC descriptors to hide controller completion
bugs. Other HCDs must satisfy the same short-transfer contract.
