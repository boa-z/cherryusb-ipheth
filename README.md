# cherryusb-ipheth

An external Apple iPhone USB Ethernet host class for CherryUSB. The protocol
core is C99 and independent of USB libraries, operating systems, and network
stacks. The included network adapter targets RT-Thread with lwIP 2.x.

This is an initial implementation, with host tests and D213 cross-builds.
**Physical iPhone traffic and unplug/replug have not yet been validated.**
Pairing/trust is an external prerequisite; this project implements no pairing,
authentication, application session, or device-wide USB configuration policy.

Features:

- Apple VID plus ff/fd/01 interface recognition, independent of PID,
  configuration value, interface number, and bulk endpoint addresses.
- Descriptor-based alternate setting selection and private endpoint ownership.
- Vendor MAC, NCM enable and carrier requests; legacy fallback only when the
  NCM enable request stalls (transport errors fail binding).
- Legacy RX (two-byte prefix), Apple NCM16 RX, and raw Ethernet TX in both modes.
- MTU 1500, MAC validation, carrier polling, error counters, RX/TX STALL recovery.
- Per-device DMA storage, bounded TX, cooperative worker shutdown, failed
  cancellation quarantine, and persistent network devices for safe reconnect.
- RT-Thread Ethernet devices i0/i1 with lwIP IPv6 Link-Local/DAD support.

Apple NCM RX does **not** require a standard CDC-NCM descriptor and does not
make TX an NCM transfer. This class does not claim USBMUX or select a different
USB configuration.

## Build and test

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

`make check sanitize` and `make check-class sanitize-class
CHERRYUSB_ROOT=/path/to/CherryUSB` are alternatives. No CherryUSB fork is vendored.

## Layout and integration

| Path | Purpose |
| --- | --- |
| `include/ipheth.h`, `src/` | Descriptor, request and packet core |
| `include/usbh_ipheth.h`, `port/cherryusb/` | Host class and network callback seam |
| `port/rtthread/` | Ethernet device, lwIP pbuf, IPv6, status command |
| `integration/` | SCons and CMake helpers |
| `tests/` | Malformed packets and mocked real-class lifecycle |
| `examples/` | Minimal application integration instructions |
| `docs/` | Architecture, porting, compatibility, validation, source licenses |

Read [porting](docs/porting.md), [architecture](docs/architecture.md),
[compatibility](docs/compatibility.md), and [hardware validation](docs/validation.md).
New code is Apache-2.0. Protocol references and their retained notices are listed
in [sources](docs/sources.md) and `LICENSES/`.
