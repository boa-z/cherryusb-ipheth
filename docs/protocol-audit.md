# Protocol and activation audit — 2026-11-10

This audit checks source, change history and host-side activation, rather than
inferring undocumented commands from one device. The driver remains independent
of pairing, application sessions and SDK-private APIs.

## References actually inspected

- Linux `drivers/net/usb/ipheth.c`: snapshot recorded in sources.md, plus the
  file's commit history fetched from the upstream GitHub API.
- FreeBSD `sys/dev/usb/net/if_ipheth.c`: snapshot recorded in sources.md.
- [Original ipheth + ipheth-pair](https://github.com/dgiagio/ipheth/tree/c8df202085804a067f403324e234a992f428afa9),
  revision `c8df202085804a067f403324e234a992f428afa9`.
- [usbmuxd preflight.c](https://github.com/libimobiledevice/usbmuxd/blob/3ded00c9985a5108cfc7591a309f9a23d57a8cba/src/preflight.c),
  revision `3ded00c9985a5108cfc7591a309f9a23d57a8cba`.
- [libimobiledevice](https://github.com/libimobiledevice/libimobiledevice/tree/fa0f79190142bc309307967c058f89c1b36eb6b8),
  revision `fa0f79190142bc309307967c058f89c1b36eb6b8`; pairing/session tools and
  public API were checked as external integration references, not vendored.

No pairing implementation or GPL helper source was copied into this component.
Community reports supplement these primary sources; they are not wire-protocol
specifications or proof of this phone's state.

## Findings and implementation decisions

| Mechanism | Evidence | Decision / remaining limit |
| --- | --- | --- |
| First successful Bulk IN before carrier/TX | Linux [bb1b40c7cb86](https://github.com/torvalds/linux/commit/bb1b40c7cb863f0800a6410c7dcb86cf3f28d3b1) explicitly describes trust-dependent TX timeouts and says iOS sends a packet once trust is established | Keep v5 readiness gate. The first successful USB completion, including empty/control transfers, establishes the observed RX state; the name is not proof that the driver authenticated anything. |
| Separate host activation from USB Ethernet | Original ipheth-pair uses libimobiledevice; usbmuxd preflight checks stored host records, opens StartSession, and handles pairing when needed | MAC/NCM success and USBMUX class binding do not establish trust or a working session. No Lockdown protocol goes in this repository. Trust of a Linux PC is not trust of the D213 host identity. |
| Exact TX transfer length | Linux [3e65efcca87a](https://github.com/torvalds/linux/commit/3e65efcca87a9bb5f3b864e0a43d167bc0a8688c) documents macOS behavior in both legacy and NCM modes: no trailing URB padding | Remove this component's added 60-byte minimum padding. Preserve caller frame length and bytes. FreeBSD still pads to its fixed TX buffer; that conflicting behavior is not chosen. This correction does not explain zero RX before readiness. |
| NCM RX only; TX remains Ethernet | Linux [a2d274c62e44](https://github.com/torvalds/linux/commit/a2d274c62e44b1995c170595db3865c6fe701226) and [be154b598fa5](https://github.com/torvalds/linux/commit/be154b598fa5) | Keep vendor request 04, 65536-byte RX, NTH16/NDP16 parsing and raw TX. No search for a standard CDC-NCM configuration; this Apple interface is vendor-specific. Mode may persist across connections. |
| Carrier reply format | Linux [67927a1b255d](https://github.com/torvalds/linux/commit/67927a1b255d883881be9467508e0af9a5e0be9d) documents 1/3/4-byte replies, including `04 03 03 00` = off and `06 04 03 04` = on | Existing second-byte-when-present parser matches Linux. First reply byte alone is insufficient. An iOS USB mode is not a USB configuration value; config value 4 does not prove device mode 4. |
| Alternate selection and request ordering | Linux probe gets MAC/enables NCM, then open selects alternate 1; FreeBSD selects alternate 1, enables NCM, then gets MAC and starts transfers | Both orderings exist. Our descriptor-selected alternate-before-NCM ordering has FreeBSD precedent and succeeds in the board control logs. Do not invent an alternate-2 retry or reorder requests as a claimed fix without a trace. |
| Legacy fallback | Linux and FreeBSD fall back when NCM enable fails | This component deliberately falls back only on STALL. A generic transport error does not reliably establish legacy mode, especially when NCM state can persist. Other failures leave Ethernet inactive while preserving composite interfaces. |
| Receive continuity and notifications | Linux [74efed51e0a4](https://github.com/torvalds/linux/commit/74efed51e0a4), [94d7eeb6c0ef](https://github.com/torvalds/linux/commit/94d7eeb6c0ef) and current callback | Keep RX armed after parser failure, ignore zero-byte/four-byte `00 01` notifications as Ethernet, and retain completion/readiness counters. |
| Legacy sizing | Linux [f33d9e2b48a3](https://github.com/torvalds/linux/commit/f33d9e2b48a34e1558b67a473a1fc1d6e793f93c) and subsequent NCM refactor distinguish TX frame limit and two-byte RX prefix | Current limits remain Ethernet 1514, legacy RX 1516, NCM RX 65536. Do not apply the old size workaround to NCM. |
| TX scheduling | Linux [94d250fae48e](https://github.com/torvalds/linux/commit/94d250fae48e6f873d8362308f5c4d02cd1b1fd2) documents stopping the queue before asynchronous submit | Our synchronous TX plus private mutex serializes submissions; no copied Linux netdev queue APIs. Future asynchronous ports must avoid the documented completion/queue race. |

## Interpretation of the actual V7 controller snapshots

The earlier live commentary mistakenly identified two addresses as self-links.
ELF symbols correct that interpretation:

- QH pool base is `4035f800`; ten 128-byte entries per bus put bus 1 QH0 at
  `4035fd00`.
- Async head base is `40362000`; 128-byte entries put bus 1's head at `40362080`.
- `head_link=4035fd02` therefore points to RX QH0, and QH0
  `link=40362082` points back to the async head. The two-node ring is valid.
- The controller ASYNCLISTADDR register can change as the controller walks
  this ring; changing between those addresses alone is not corruption.

| Observation | Supported conclusion |
| --- | --- |
| IRQ 36 -> 54; FRAME 0fc9 -> 1b68; descriptor reads succeed | Controller time and IRQ handling progress; known EP0 traffic works while RX is pending. A global frozen controller is not supported by this evidence. |
| CMD 00010031 and STS e008/c008 | Run and asynchronous schedule are enabled; the snapshot does not show a halted controller or Host System Error. |
| RX URB err=-6 | This is USB_ERR_BUSY, the normal in-progress state, not a completed USB error. |
| Four qTD tokens remain 40008d80 | Each is Active, IN, IOC with 16384 bytes remaining; no byte progress or terminal halt/error is shown. It is not the previously reproduced completed-short-first-qTD scenario. |
| No RX completion, no TX attempt | Consistent with a phone-side activation prerequisite, but not proof of one. Endpoint-specific scheduling/DMA faults remain possible; tokens alone are not a USB handshake capture. |

The standalone short-IN HCD patch remains a tested general correction. The
current board evidence does not justify blaming this outstanding RX on it or
adding further queue/cache patches based on the misidentified addresses.

## Next validation: keep driver and activation independent

Use the existing V7 image to establish host activation evidence before asking
for another flash. Existing application commands can exercise USBMUX and its
external pairing/session layer:

```text
carplay_usb mux
carplay_usb pair
ipheth_status
usb_ehci_status 1
```

Unlock the phone and respond to its trust dialog, if one appears. Record the
exact USBMUX negotiation and pairing/session stage, not just whether endpoints
bind. A session already being open needs no new pairing attempt; do not remove
trust records or close sessions merely to run this checklist. The application's
pair command also tries an application service after session setup: failure of
that later service is not equivalent to missing Ethernet trust, and that
service is not an IPHETH requirement. If the existing -4 timeout remains at an
early USBMUX/Lockdown stage, investigate that separate external dependency
without implementing pairing in the driver.

If a Linux reference host is available, capture a baseline with the same phone
and cable using current ipheth + usbmuxd/libimobiledevice. Compare interface
class/configuration, vendor requests, successful session establishment, first
Bulk IN and network traffic using usbmon. Personal-hotspot success in a different
configuration is a useful device/cable baseline, not proof of CarPlay-config
behavior or D213 trust. A Linux pair record must not be assumed valid for a
new D213 identity. A PC baseline is not required to use the portable driver,
but gives substantially stronger evidence than repeatedly varying alt or NCM
buffer size on the board.

## Confidence and validation limits

Plain CTest and ASan/UBSan suites cover the TX correction at 14, 42, 60, 64,
70, 512 and 1514 bytes, exact capacities, byte preservation and a canary beyond
the submitted length. Class tests require actual 42-byte USB OUT length in
both NCM and legacy cases. The historical 70-byte timeout is unchanged by this
padding correction. Hardware RX/TX and reconnect stress remain unproven.

Community [libimobiledevice #299](https://github.com/libimobiledevice/libimobiledevice/issues/299)
corroborates the external-pairing boundary; it is an older discussion, not a
current protocol specification. [usbmuxd #285](https://github.com/libimobiledevice/usbmuxd/issues/285)
reports Linux NO-CARRIER despite reportedly successful pairing. That unverified
report is a reminder that pairing success alone is not sufficient evidence of
Ethernet readiness; it is not assumed to match this phone/model/iOS version.
