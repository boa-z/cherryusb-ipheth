# Source and protocol provenance

Research date: 2026-11-10. Reference files are outside this repository; this
repository contains no complete USB stack or operating-system fork.

| Reference | Revision inspected | What it established |
| --- | --- | --- |
| [Linux ipheth.c](https://github.com/torvalds/linux/blob/067c5c24cef23d913b67eaaf71ae67e7169a6620/drivers/net/usb/ipheth.c) | Linux master HEAD recorded as `067c5c24cef23d913b67eaaf71ae67e7169a6620` | ff/fd/01, vendor commands 00/04/45, legacy RX prefix, Apple NCM RX only, raw TX, two carrier reply variants, first-successful-RX readiness before carrier/TX, empty/control RX filtering |
| [FreeBSD if_ipheth.c](https://github.com/freebsd/freebsd-src/blob/2d5a6bc2a4c5eb061f8d63b7aaff7dc04e96f1ea/sys/dev/usb/net/if_ipheth.c) | main HEAD recorded as `2d5a6bc2a4c5eb061f8d63b7aaff7dc04e96f1ea` | Request index from discovered interface, alternate setting, independent Ethernet attachment, packet validation |
| [CherryUSB](https://github.com/cherry-embedded/CherryUSB/tree/a0054795c44037ace8a2b77b6e610adbff2257ea) | `a0054795c44037ace8a2b77b6e610adbff2257ea` | External class API, URBs, OSAL, section registration |
| [Luban-Lite SDK](https://github.com/jiachenin/luban-lite-jc) | local `poc/carplay-oss`, base `d14f4803d` | Original vendor 1.5 control-return mismatch; upgraded to official a0054795 with separate SDK overlay, RT-Thread/lwIP 2.1.2, class KEEP and SCons integration |
| [RT-Thread](https://github.com/RT-Thread/rt-thread) | SDK-vendored `kernel/rt-thread/` | eth_device registration, link notification, netif lifetime, pbuf and IPv6 initialization |
| [rt-carplay](https://github.com/boa-z/rt-carplay) | local main, initial `db086d1` | Application-side configuration selection and public network integration only; no source copied into the driver |

The Linux and FreeBSD files were downloaded from their branch raw URLs; HEADs
above were recorded in the same research session. Exact downloaded SHA-256:

```text
Linux:   b2afefacb501e91ccab18b0bd6ca52767588d351701256afeae8a53088ba1d78
FreeBSD: e50e6f3ba7ce63c6304c3cdbc98bf5a2fbc2aa67b87ccfa9f68d94653bacef6b
```

New implementation files are Apache-2.0. Linux ipheth provides BSD-3-Clause
terms as an alternative to GPL-2.0-only; the behavior reference uses those BSD
terms. Its original notice is retained in `LICENSES/BSD-3-Clause-ipheth.txt`.
FreeBSD's notice is retained in `LICENSES/BSD-2-Clause-if_ipheth.txt`. The source
is newly written, not a copied Linux/FreeBSD driver or a relicensed USB stack.
Original third-party notices are preserved independently from new-code licensing.

Intentional differences: dynamically discover interface/endpoints/alternate;
no PID list or assumed interface 2; complete NCM validation before delivery;
explicit length/overlap/termination checks; per-device resources; cooperative
shutdown and failed-cancel quarantine. TX follows current Linux and the Apple
driver's observed behavior: submit the original Ethernet length with no added
trailing padding. FreeBSD's fixed 1514-byte padded TX behavior is not adopted.
The earlier 60-byte minimum padding was removed after reviewing Linux commit
3e65efcca87a in the protocol audit.

## Community search

GitHub searches returned no matches for:

- `ipheth` code and issues/PRs in `cherry-embedded/CherryUSB`.
- `"Apple Ethernet"` issues/PRs in that repository.
- Global code/repository search combining `CherryUSB` and `ipheth`.
- `IPHETH` code search in `RT-Thread/rt-thread`.

These are recorded search results, not a claim that no private implementation,
unindexed patch, alternate spelling or external downstream implementation exists.
Repeat the searches before proposing an upstream PR.

## Upstream preparation

The driver uses the external Class Driver API without a CherryUSB core patch.
Protocol and RT-Thread code are separate; the core has no hardware or business
API dependency. Pinned-header tests, sanitizer tests, provenance, porting and
hardware validation instructions are supplied. The public repository is https://github.com/boa-z/cherryusb-ipheth; changes are
committed and pushed with the owner's authorization.

Before upstream submission: obtain actual iPhone RX/TX captures for both modes,
verify DMA/cache and disconnect stress on the target HCD, confirm multiple phone
models/configurations, review memory cost and lifetime policy with maintainers,
and adapt file placement/build style to upstream preference. The public CI has passed both plain and ASan/UBSan matrix jobs. See
[protocol-audit.md](protocol-audit.md) for the mechanism audit and evidence limits.
